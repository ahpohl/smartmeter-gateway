#include "meter.h"
#include "config.h"
#include "config_yaml.h"
#include "json_utils.h"
#include "meter_types.h"
#include "modbus_error.h"
#include "signal_handler.h"
#include <algorithm>
#include <asm-generic/ioctls.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <expected>
#include <mutex>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>

using json = nlohmann::ordered_json;

Meter::Meter(const MeterConfig &cfg, SignalHandler &signalHandler)
    : cfg_(cfg), handler_(signalHandler) {

  meterLogger_ = spdlog::get("meter");
  if (!meterLogger_)
    meterLogger_ = spdlog::default_logger();

  // Start update loop thread
  worker_ = std::thread(&Meter::runLoop, this);
}

Meter::~Meter() {
  cv_.notify_all();
  if (worker_.joinable())
    worker_.join();
  disconnect();
}

void Meter::disconnect(void) {
  {
    if (serialPort_ != -1) {
      close(serialPort_);
      serialPort_ = -1;

      if (availabilityCallback_)
        availabilityCallback_("disconnected");

      meterLogger_->info("Meter disconnected");
    }
  }
}

void Meter::setUpdateCallback(
    std::function<void(std::string, MeterTypes::Values)> cb) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  updateCallback_ = std::move(cb);
}

void Meter::setDeviceCallback(
    std::function<void(std::string, MeterTypes::Device)> cb) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  deviceCallback_ = std::move(cb);
}

void Meter::setAvailabilityCallback(std::function<void(std::string)> cb) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  availabilityCallback_ = std::move(cb);
}

MeterTypes::ErrorAction
Meter::handleResult(std::expected<void, ModbusError> &&result) {
  if (result) {
    return MeterTypes::ErrorAction::NONE;
  }

  const ModbusError &err = result.error();

  if (err.severity == ModbusError::Severity::FATAL) {
    // Fatal error occurred - initiate shutdown sequence
    meterLogger_->error("FATAL Meter error: {}", err.describe());
    handler_.shutdown();
    return MeterTypes::ErrorAction::SHUTDOWN;

  } else if (err.severity == ModbusError::Severity::TRANSIENT) {
    // Temporary error - disconnect, wait and reconnect
    meterLogger_->warn("Transient Meter error: {}", err.describe());
    disconnect();
    return MeterTypes::ErrorAction::RECONNECT;

  } else if (err.severity == ModbusError::Severity::SHUTDOWN) {
    // Shutdown already in progress - just exit cleanly
    meterLogger_->trace("Meter operation cancelled due to shutdown: {}",
                        err.describe());
    return MeterTypes::ErrorAction::SHUTDOWN;
  }

  return MeterTypes::ErrorAction::NONE;
}

std::expected<void, ModbusError> Meter::tryConnect(void) {
  if (!handler_.isRunning()) {
    return std::unexpected(
        ModbusError::custom(EINTR, "tryConnect(): Shutdown in progress"));
  }

  if (serialPort_ >= 0)
    return {};

  serialPort_ = open(cfg_.device.c_str(), O_RDONLY | O_NOCTTY);
  if (serialPort_ == -1) {
    return std::unexpected(
        ModbusError::fromErrno("Opening serial device failed"));
  }

  if (!isatty(serialPort_)) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(ModbusError::fromErrno("Device is not a tty"));
  }

  if (flock(serialPort_, LOCK_EX | LOCK_NB) == -1) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(
        ModbusError::fromErrno("Failed to lock serial device"));
  }

  if (ioctl(serialPort_, TIOCEXCL) == -1) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(
        ModbusError::fromErrno("Failed to set exclusive lock"));
  }

  termios serialPortSettings;
  if (tcgetattr(serialPort_, &serialPortSettings) == -1) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(
        ModbusError::fromErrno("Failed to get serial port attributes"));
  }

  cfmakeraw(&serialPortSettings);

  // set baud (both directions)
  speed_t baudSpeed = MeterTypes::baudToSpeed(cfg_.baud);
  if (cfsetispeed(&serialPortSettings, baudSpeed) < 0 ||
      cfsetospeed(&serialPortSettings, baudSpeed) < 0) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(ModbusError::fromErrno(
        "Failed to set serial port speed {} baud", cfg_.baud));
  }

  // Base flags: enable receiver, ignore modem control lines
  serialPortSettings.c_cflag |= (CLOCAL | CREAD);

  // Clear size/parity/stop/flow flags first to avoid unexpected bits
  serialPortSettings.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);

  // Set data bits
  serialPortSettings.c_cflag |= MeterTypes::dataBitsToFlag(cfg_.dataBits);

  // Set parity
  switch (cfg_.parity) {
  case MeterTypes::Parity::Even:
    serialPortSettings.c_cflag |= PARENB;
    serialPortSettings.c_cflag &= ~PARODD;
    break;
  case MeterTypes::Parity::Odd:
    serialPortSettings.c_cflag |= PARENB;
    serialPortSettings.c_cflag |= PARODD;
    break;
  case MeterTypes::Parity::None:
  default:
    // PARENB already cleared above
    break;
  }

  // Set stop bits (2 stop bits if stopBits == 2, otherwise 1)
  if (cfg_.stopBits == 2) {
    serialPortSettings.c_cflag |= CSTOPB;
  }

  // blocking read: wait until buffer has been filled, 0.5s
  // timeout for first byte (VTIME=5)
  serialPortSettings.c_cc[VMIN] = BUFFER_SIZE;
  serialPortSettings.c_cc[VTIME] = 5;

  if (tcsetattr(serialPort_, TCSANOW, &serialPortSettings)) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(
        ModbusError::fromErrno("Failed to set serial port attributes"));
  }

  // flush both directions if desired after applying settings
  tcflush(serialPort_, TCIOFLUSH);

  meterLogger_->info("Meter connected ({}{}{}, {} baud)", cfg_.dataBits,
                     MeterTypes::parityToChar(cfg_.parity), cfg_.stopBits,
                     cfg_.baud);

  if (availabilityCallback_)
    availabilityCallback_("connected");

  {
    std::unique_lock<std::mutex> lock(cbMutex_);
    cv_.wait_for(lock, std::chrono::seconds(1),
                 [this] { return !handler_.isRunning(); });
  }

  return {};
}

std::expected<void, ModbusError> Meter::readTelegram() {
  if (!handler_.isRunning()) {
    return std::unexpected(
        ModbusError::custom(EINTR, "readTelegram(): Shutdown in progress"));
  }

  if (serialPort_ == -1)
    return std::unexpected(
        ModbusError::custom(ENOTCONN, "readTelegram(): Meter not connected"));

  std::vector<char> buffer(BUFFER_SIZE);
  std::vector<char> packet(TELEGRAM_SIZE);
  size_t packetPos = 0;
  bool messageBegin = false;
  bool telegramComplete = false;

  // In readTelegram():
  while (packetPos < TELEGRAM_SIZE && !telegramComplete) {
    // Add shutdown check BEFORE blocking read
    if (!handler_.isRunning()) {
      return std::unexpected(
          ModbusError::custom(EINTR, "readTelegram(): Shutdown in progress"));
    }

    std::fill(buffer.begin(), buffer.end(), '\0');
    ssize_t bytesReceived = ::read(serialPort_, buffer.data(), BUFFER_SIZE);

    if (bytesReceived == -1) {
      return std::unexpected(
          ModbusError::fromErrno("Failed to read serial device"));
    }

    if (bytesReceived == 0) {
      // Timeout - shouldn't happen mid-telegram
      return std::unexpected(ModbusError::custom(
          ETIMEDOUT, "readTelegram(): Timeout during read"));
    }

    // Process bytes
    for (ssize_t i = 0; i < bytesReceived && packetPos < TELEGRAM_SIZE; ++i) {
      char c = buffer[i];
      if (c == '/')
        messageBegin = true;
      if (messageBegin) {
        packet[packetPos++] = c;
        if (packetPos >= 3 && packet[packetPos - 3] == '!') {
          telegramComplete = true;
          break;
        }
      }
    }
  }

  // Ensure we have at least 3 bytes and the third-from-last is '!'
  if (packetPos < 3 || packet[packetPos - 3] != '!') {
    return std::unexpected(ModbusError::custom(
        EPROTO, "readTelegram(): telegram stream not in sync"));
  }

  meterLogger_->trace("Received telegram (len {}):\n{}", packetPos,
                      std::string(packet.begin(), packet.begin() + packetPos));

  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    telegram_.assign(packet.begin(), packet.begin() + packetPos);
  }

  return {};
}

std::expected<void, ModbusError> Meter::updateValuesAndJson() {
  if (!handler_.isRunning()) {
    return std::unexpected(ModbusError::custom(
        EINTR, "updateValuesAndJson(): Shutdown in progress"));
  }
  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    if (telegram_.empty())
      return {};
  }

  MeterTypes::Values values{};

  std::istringstream iss;
  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    iss.str(telegram_);
  }

  values.time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  std::regex obexRegex(R"(^([0-9]-0:[0-9]+.[0-9]+.[0-9]+\*255)\(([^)]+)\))");
  std::string line;

  int lineNum = 0;
  while (std::getline(iss, line)) {
    ++lineNum;
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

    if (line.empty() || (line.size() && (line[0] == '/' || line[0] == '!')))
      continue;

    try {
      std::smatch match;
      if (!std::regex_search(line, match, obexRegex))
        throw std::invalid_argument("Malformed OBEX expression");

      std::string obis = match[1];
      std::string value_unit = match[2];

      if (obis == "1-0:1.8.0*255") {
        size_t pos = value_unit.find("*");
        values.energy = std::stod(value_unit.substr(0, pos));
      } else if (obis == "1-0:16.7.0*255") {
        size_t pos = value_unit.find("*");
        values.activePower = std::stod(value_unit.substr(0, pos));
      } else if (obis == "1-0:36.7.0*255") {
        size_t pos = value_unit.find("*");
        values.phase1.activePower = std::stod(value_unit.substr(0, pos));
      } else if (obis == "1-0:56.7.0*255") {
        size_t pos = value_unit.find("*");
        values.phase2.activePower = std::stod(value_unit.substr(0, pos));
      } else if (obis == "1-0:76.7.0*255") {
        size_t pos = value_unit.find("*");
        values.phase3.activePower = std::stod(value_unit.substr(0, pos));
      } else if (obis == "1-0:32.7.0*255") {
        size_t pos = value_unit.find("*");
        values.phase1.phVoltage = std::stod(value_unit.substr(0, pos));
      } else if (obis == "1-0:52.7.0*255") {
        size_t pos = value_unit.find("*");
        values.phase2.phVoltage = std::stod(value_unit.substr(0, pos));
      } else if (obis == "1-0:72.7.0*255") {
        size_t pos = value_unit.find("*");
        values.phase3.phVoltage = std::stod(value_unit.substr(0, pos));
      } else if (obis == "0-0:96.8.0*255") {
        size_t pos = value_unit.find("*");
        values.activeSensorTime =
            std::stoul(value_unit.substr(0, pos), nullptr, 16);
      }
    } catch (const std::exception &err) {
      std::ostringstream oss;
      oss << "[" << line << "]: " << err.what();
      return std::unexpected(ModbusError::custom(EPROTO, oss.str()));
    }
  }

  // power factor and frequency (assumed)
  values.powerFactor = 0.95;
  values.frequency = 50.0;

  if (cfg_.grid) {
    values.powerFactor = cfg_.grid->powerFactor;
    values.frequency = cfg_.grid->frequency;
  }
  values.phase1.powerFactor = values.powerFactor;
  values.phase2.powerFactor = values.powerFactor;
  values.phase3.powerFactor = values.powerFactor;

  // apparent power
  values.apparentPower = values.activePower / values.powerFactor;
  values.phase1.apparentPower =
      values.phase1.activePower / values.phase1.powerFactor;
  values.phase2.apparentPower =
      values.phase2.activePower / values.phase2.powerFactor;
  values.phase3.apparentPower =
      values.phase3.activePower / values.phase3.powerFactor;

  // reactive power
  values.reactivePower =
      std::tan(std::acos(values.powerFactor)) * values.activePower;
  values.phase1.reactivePower = std::tan(std::acos(values.phase1.powerFactor)) *
                                values.phase1.activePower;
  values.phase2.reactivePower = std::tan(std::acos(values.phase2.powerFactor)) *
                                values.phase2.activePower;
  values.phase3.reactivePower = std::tan(std::acos(values.phase3.powerFactor)) *
                                values.phase3.activePower;

  // voltages
  values.phVoltage = (values.phase1.phVoltage + values.phase2.phVoltage +
                      values.phase3.phVoltage) /
                     3.0;
  values.phase1.ppVoltage =
      std::sqrt(values.phase1.phVoltage * values.phase1.phVoltage +
                values.phase2.phVoltage * values.phase2.phVoltage +
                values.phase1.phVoltage * values.phase2.phVoltage);
  values.phase2.ppVoltage =
      std::sqrt(values.phase2.phVoltage * values.phase2.phVoltage +
                values.phase3.phVoltage * values.phase3.phVoltage +
                values.phase2.phVoltage * values.phase3.phVoltage);
  values.phase3.ppVoltage =
      std::sqrt(values.phase3.phVoltage * values.phase3.phVoltage +
                values.phase1.phVoltage * values.phase1.phVoltage +
                values.phase3.phVoltage * values.phase1.phVoltage);
  values.ppVoltage = (values.phase1.ppVoltage + values.phase2.ppVoltage +
                      values.phase3.ppVoltage) /
                     3.0;

  // currents
  values.phase1.current = values.phase1.activePower /
                          (values.phase1.phVoltage * values.powerFactor);
  values.phase2.current = values.phase2.activePower /
                          (values.phase2.phVoltage * values.powerFactor);
  values.phase3.current = values.phase3.activePower /
                          (values.phase3.phVoltage * values.powerFactor);
  values.current =
      values.phase1.current + values.phase2.current + values.phase3.current;

  json newJson;
  json phases = json::array();

  phases.push_back({
      {"id", 1},
      {"power_active", JsonUtils::roundTo(values.phase1.activePower, 2)},
      {"power_apparent", JsonUtils::roundTo(values.phase1.apparentPower, 2)},
      {"power_reactive", JsonUtils::roundTo(values.phase1.reactivePower, 2)},
      {"power_factor", JsonUtils::roundTo(values.phase1.powerFactor, 2)},
      {"voltage_ph", JsonUtils::roundTo(values.phase1.phVoltage, 1)},
      {"voltage_pp", JsonUtils::roundTo(values.phase1.ppVoltage, 1)},
      {"current", JsonUtils::roundTo(values.phase1.current, 3)},

  });

  phases.push_back({
      {"id", 2},
      {"power_active", JsonUtils::roundTo(values.phase2.activePower, 2)},
      {"power_apparent", JsonUtils::roundTo(values.phase2.apparentPower, 2)},
      {"power_reactive", JsonUtils::roundTo(values.phase2.reactivePower, 2)},
      {"power_factor", JsonUtils::roundTo(values.phase2.powerFactor, 2)},
      {"voltage_ph", JsonUtils::roundTo(values.phase2.phVoltage, 1)},
      {"voltage_pp", JsonUtils::roundTo(values.phase2.ppVoltage, 1)},
      {"current", JsonUtils::roundTo(values.phase2.current, 3)},

  });

  phases.push_back({
      {"id", 3},
      {"power_active", JsonUtils::roundTo(values.phase3.activePower, 2)},
      {"power_apparent", JsonUtils::roundTo(values.phase3.apparentPower, 2)},
      {"power_reactive", JsonUtils::roundTo(values.phase3.reactivePower, 2)},
      {"power_factor", JsonUtils::roundTo(values.phase3.powerFactor, 2)},
      {"voltage_ph", JsonUtils::roundTo(values.phase3.phVoltage, 1)},
      {"voltage_pp", JsonUtils::roundTo(values.phase3.ppVoltage, 1)},
      {"current", JsonUtils::roundTo(values.phase3.current, 3)},
  });

  newJson["time"] = values.time;
  newJson["energy"] = JsonUtils::roundTo(values.energy, 6);
  newJson["power_active"] = JsonUtils::roundTo(values.activePower, 2);
  newJson["power_apparent"] = JsonUtils::roundTo(values.apparentPower, 2);
  newJson["power_reactive"] = JsonUtils::roundTo(values.reactivePower, 2);
  newJson["power_factor"] = JsonUtils::roundTo(values.powerFactor, 2);
  newJson["phases"] = phases;
  newJson["active_time"] = values.activeSensorTime;
  newJson["frequency"] = JsonUtils::roundTo(values.frequency, 2);
  newJson["voltage_ph"] = JsonUtils::roundTo(values.phVoltage, 1);
  newJson["voltage_pp"] = JsonUtils::roundTo(values.ppVoltage, 1);

  // Update shared values and JSON with lock
  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    values_ = std::move(values);
    jsonValues_ = std::move(newJson);
  }

  meterLogger_->debug("{}", jsonValues_.dump());

  return {};
}

std::expected<void, ModbusError> Meter::updateDeviceAndJson() {
  if (!handler_.isRunning()) {
    return std::unexpected(ModbusError::custom(
        EINTR, "updateDeviceAndJson(): Shutdown in progress"));
  }

  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    if (telegram_.empty())
      return {};
  }

  MeterTypes::Device newDevice{};

  std::istringstream iss;
  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    iss.str(telegram_);
  }

  std::regex obexRegex(R"(^([0-9]-0:[0-9]+.[0-9]+.[0-9]+\*255)\(([^)]+)\))");
  std::regex versionRegex(R"(^(\/[A-Za-z0-9]+)_([A-Za-z0-9]+)$)");
  std::string line;

  int lineNum = 0;
  while (std::getline(iss, line)) {
    ++lineNum;
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

    if (line.empty() || line[0] == '!')
      continue;

    try {
      // Version line
      if (!line.empty() && line[0] == '/') {
        std::smatch versionMatch;
        if (!std::regex_search(line, versionMatch, versionRegex)) {
          throw std::invalid_argument("Malformed version expression");
        }
        newDevice.fwVersion = versionMatch[2].str();
        continue;
      }

      // OBEX line
      std::smatch obexMatch;
      if (!std::regex_search(line, obexMatch, obexRegex)) {
        throw std::invalid_argument("Malformed OBEX expression");
      }

      std::string obis = obexMatch[1];
      if (obis == "1-0:96.1.0*255") {
        newDevice.serialNumber = obexMatch[2].str();
      } else if (obis == "1-0:96.5.0*255") {
        newDevice.status = obexMatch[2].str();
      }

    } catch (const std::exception &err) {
      std::ostringstream oss;
      oss << "[" << line << "]: " << err.what();
      return std::unexpected(ModbusError::custom(EPROTO, oss.str()));
    }
  }

  newDevice.manufacturer = "EasyMeter";
  newDevice.model = "DD3-BZ06-ETA-ODZ1";
  newDevice.options = std::string(PROJECT_VERSION) + "-" + GIT_COMMIT_HASH;
  newDevice.phases = 3;

  // ---- Build ordered JSON ----
  json newJson;

  newJson["manufacturer"] = newDevice.manufacturer;
  newJson["model"] = newDevice.model;
  newJson["serial_number"] = newDevice.serialNumber;
  newJson["firmware_version"] = newDevice.fwVersion;
  newJson["options"] = newDevice.options;
  newJson["phases"] = newDevice.phases;
  newJson["status"] = newDevice.status;

  meterLogger_->debug("{}", newJson.dump());

  // ---- Commit values ----
  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    jsonDevice_ = std::move(newJson);
    device_ = std::move(newDevice);
  }

  return {};
}

void Meter::runLoop() {

  while (handler_.isRunning()) {

    // Connect to meter
    auto connectAction = handleResult(tryConnect());
    if (connectAction == MeterTypes::ErrorAction::SHUTDOWN)
      break;

    // Read telegram - on any error, loop restarts (will try reconnect)
    auto readAction = handleResult(readTelegram());
    if (readAction == MeterTypes::ErrorAction::SHUTDOWN)
      break;
    else if (readAction == MeterTypes::ErrorAction::RECONNECT)
      continue;

    // Update device
    auto deviceAction = handleResult(updateDeviceAndJson());
    if (deviceAction == MeterTypes::ErrorAction::SHUTDOWN)
      break;
    else if (deviceAction == MeterTypes::ErrorAction::RECONNECT)
      continue;

    if (handler_.isRunning()) {
      std::lock_guard<std::mutex> lock(cbMutex_);
      if (deviceCallback_) {
        deviceCallback_(jsonDevice_.dump(), device_);
      }
    }

    // Update values
    auto updateAction = handleResult(updateValuesAndJson());
    if (updateAction == MeterTypes::ErrorAction::SHUTDOWN)
      break;
    else if (updateAction == MeterTypes::ErrorAction::RECONNECT)
      continue;

    if (handler_.isRunning()) {
      std::lock_guard<std::mutex> lock(cbMutex_);
      if (updateCallback_) {
        updateCallback_(jsonValues_.dump(), values_);
      }
    }
  }

  meterLogger_->debug("Meter run loop stopped.");
}