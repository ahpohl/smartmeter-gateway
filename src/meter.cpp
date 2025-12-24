#include "meter.h"
#include "config.h"
#include "config_yaml.h"
#include "json_utils.h"
#include "meter_error.h"
#include "meter_types.h"
#include "signal_handler.h"
#include <algorithm>
#include <asm-generic/ioctls.h>
#include <chrono>
#include <expected>
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

void Meter::setUpdateCallback(std::function<void(const std::string &)> cb) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  updateCallback_ = std::move(cb);
}

void Meter::setDeviceCallback(std::function<void(const std::string &)> cb) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  deviceCallback_ = std::move(cb);
}

void Meter::setAvailabilityCallback(
    std::function<void(const std::string &)> cb) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  availabilityCallback_ = std::move(cb);
}

Meter::ErrorAction
Meter::handleResult(std::expected<void, MeterError> &&result) {
  if (result) {
    return Meter::ErrorAction::NONE;
  }

  const MeterError &err = result.error();

  if (err.severity == MeterError::Severity::FATAL) {
    // Fatal error occurred - initiate shutdown sequence
    meterLogger_->error("FATAL Meter error: {}", err.describe());
    handler_.shutdown();
    return Meter::ErrorAction::SHUTDOWN;

  } else if (err.severity == MeterError::Severity::TRANSIENT) {
    // Temporary error - disconnect and reconnect
    meterLogger_->warn("Transient Meter error: {}", err.describe());
    disconnect();
    return Meter::ErrorAction::RECONNECT;

  } else if (err.severity == MeterError::Severity::SHUTDOWN) {
    // Shutdown already in progress - just exit cleanly
    meterLogger_->trace("Meter operation cancelled due to shutdown: {}",
                        err.describe());
    return Meter::ErrorAction::SHUTDOWN;
  }

  return Meter::ErrorAction::NONE;
}

std::expected<void, MeterError> Meter::tryConnect(void) {
  if (!handler_.isRunning()) {
    return std::unexpected(
        MeterError::custom(EINTR, "tryConnect(): Shutdown in progress"));
  }

  if (serialPort_ >= 0)
    return {};

  serialPort_ = open(cfg_.device.c_str(), O_RDONLY | O_NOCTTY);
  if (serialPort_ == -1) {
    return std::unexpected(
        MeterError::fromErrno("Opening serial device failed"));
  }

  if (!isatty(serialPort_)) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(MeterError::fromErrno("Device is not a tty"));
  }

  if (flock(serialPort_, LOCK_EX | LOCK_NB) == -1) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(
        MeterError::fromErrno("Failed to lock serial device"));
  }

  if (ioctl(serialPort_, TIOCEXCL) == -1) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(
        MeterError::fromErrno("Failed to set exclusive lock"));
  }

  termios serialPortSettings;
  if (tcgetattr(serialPort_, &serialPortSettings) == -1) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(
        MeterError::fromErrno("Failed to get serial port attributes"));
  }

  cfmakeraw(&serialPortSettings);

  // set baud (both directions)
  if (cfsetispeed(&serialPortSettings, B9600) < 0 ||
      cfsetospeed(&serialPortSettings, B9600) < 0) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(
        MeterError::fromErrno("Failed to set serial port speed"));
  }

  // Base flags: enable receiver, ignore modem control lines
  serialPortSettings.c_cflag |= (CLOCAL | CREAD);

  // Clear size/parity/stop/flow flags first to avoid unexpected bits
  serialPortSettings.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);

  // 7 data bits
  serialPortSettings.c_cflag |= CS7;

  // Even parity: enable PARENB, ensure PARODD cleared
  serialPortSettings.c_cflag |= PARENB;
  serialPortSettings.c_cflag &= ~PARODD;

  // Non-blocking read:  return immediately with available data (VMIN=0), 0.5s
  // timeout for first byte (VTIME=5)
  serialPortSettings.c_cc[VMIN] = BUFFER_SIZE;
  serialPortSettings.c_cc[VTIME] = 5;

  if (tcsetattr(serialPort_, TCSANOW, &serialPortSettings)) {
    int saved_errno = errno;
    close(serialPort_);
    errno = saved_errno;
    return std::unexpected(
        MeterError::fromErrno("Failed to set serial port attributes"));
  }

  // flush both directions if desired after applying settings
  tcflush(serialPort_, TCIOFLUSH);

  meterLogger_->info("Meter connected");
  if (availabilityCallback_)
    availabilityCallback_("connected");

  return {};
}

std::expected<void, MeterError> Meter::readTelegram() {
  if (!handler_.isRunning()) {
    return std::unexpected(
        MeterError::custom(EINTR, "readTelegram(): Shutdown in progress"));
  }

  if (serialPort_ == -1)
    return std::unexpected(
        MeterError::custom(ENOTCONN, "readTelegram(): Meter not connected"));

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
          MeterError::custom(EINTR, "readTelegram(): Shutdown in progress"));
    }

    std::fill(buffer.begin(), buffer.end(), '\0');
    ssize_t bytesReceived = ::read(serialPort_, buffer.data(), BUFFER_SIZE);

    if (bytesReceived == -1) {
      return std::unexpected(
          MeterError::fromErrno("Failed to read serial device"));
    }

    if (bytesReceived == 0) {
      // Timeout - shouldn't happen mid-telegram
      return std::unexpected(
          MeterError::custom(ETIMEDOUT, "readTelegram(): Timeout during read"));
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
    return std::unexpected(MeterError::custom(
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

std::expected<void, MeterError> Meter::updateValuesAndJson() {
  if (!handler_.isRunning()) {
    return std::unexpected(MeterError::custom(
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

  std::regex lineRegex(R"(^([0-9]-0:[0-9]+.[0-9]+.[0-9]+\*255)\(([^)]+)\))");
  std::string line;

  int lineNum = 0;
  while (std::getline(iss, line)) {
    ++lineNum;
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

    if (line.empty() || (line.size() && (line[0] == '/' || line[0] == '!')))
      continue;

    try {
      std::smatch match;
      if (std::regex_search(line, match, lineRegex)) {
        std::string obis = match[1];
        std::string value_unit = match[2];

        if (obis == "1-0:1.8.0*255") {
          size_t pos = value_unit.find("*");
          values.energy = std::stod(value_unit.substr(0, pos));
        } else if (obis == "1-0:16.7.0*255") {
          size_t pos = value_unit.find("*");
          values.power = std::stod(value_unit.substr(0, pos));
        } else if (obis == "1-0:36.7.0*255") {
          size_t pos = value_unit.find("*");
          values.phase1.power = std::stod(value_unit.substr(0, pos));
        } else if (obis == "1-0:56.7.0*255") {
          size_t pos = value_unit.find("*");
          values.phase2.power = std::stod(value_unit.substr(0, pos));
        } else if (obis == "1-0:76.7.0*255") {
          size_t pos = value_unit.find("*");
          values.phase3.power = std::stod(value_unit.substr(0, pos));
        } else if (obis == "1-0:32.7.0*255") {
          size_t pos = value_unit.find("*");
          values.phase1.voltage = std::stod(value_unit.substr(0, pos));
        } else if (obis == "1-0:52.7.0*255") {
          size_t pos = value_unit.find("*");
          values.phase2.voltage = std::stod(value_unit.substr(0, pos));
        } else if (obis == "1-0:72.7.0*255") {
          size_t pos = value_unit.find("*");
          values.phase3.voltage = std::stod(value_unit.substr(0, pos));
        } else if (obis == "0-0:96.8.0*255") {
          size_t pos = value_unit.find("*");
          values.activeSensorTime =
              std::stoul(value_unit.substr(0, pos), nullptr, 16);
        }
      } else {
        throw std::invalid_argument("Malformed OBEX expression");
      }
    } catch (const std::exception &err) {
      std::ostringstream oss;
      oss << "[" << line << "]: " << err.what();
      return std::unexpected(MeterError::custom(EPROTO, oss.str()));
    }
  }

  values.time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  json newJson;
  json phases = json::array();

  phases.push_back({
      {"id", 1},
      {"power", JsonUtils::roundTo(values.phase1.power, 0)},
      {"voltage", JsonUtils::roundTo(values.phase1.voltage, 2)},
  });

  phases.push_back({
      {"id", 2},
      {"power", JsonUtils::roundTo(values.phase2.power, 0)},
      {"voltage", JsonUtils::roundTo(values.phase2.voltage, 2)},
  });

  phases.push_back({
      {"id", 3},
      {"power", JsonUtils::roundTo(values.phase3.power, 0)},
      {"voltage", JsonUtils::roundTo(values.phase3.voltage, 2)},
  });

  newJson["time"] = values.time;
  newJson["energy"] = JsonUtils::roundTo(values.energy, 1);
  newJson["power"] = JsonUtils::roundTo(values.power, 0);
  newJson["phases"] = phases;
  newJson["active_time"] = values.activeSensorTime;

  // Update shared values and JSON with lock
  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    values_ = std::move(values);
    jsonValues_ = std::move(newJson);
  }

  meterLogger_->debug("{}", jsonValues_.dump());

  return {};
}

std::expected<void, MeterError> Meter::updateDeviceAndJson() {
  if (!handler_.isRunning()) {
    return std::unexpected(MeterError::custom(
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

  std::regex lineRegex(R"(^([0-9]-0:[0-9]+.[0-9]+.[0-9]+\*255)\(([^)]+)\))");
  std::string line;

  int lineNum = 0;
  while (std::getline(iss, line)) {
    ++lineNum;
    line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());

    if (line.empty() || (line.size() && (line[0] == '/' || line[0] == '!')))
      continue;

    try {
      std::smatch match;
      if (std::regex_search(line, match, lineRegex)) {
        std::string obis = match[1];

        if (obis == "1-0:96.1.0*255") {
          newDevice.serialNumber = match[2];
        } else if (obis == "1-0:96.5.0*255") {
          newDevice.status = match[2];
        }
      } else {
        throw std::invalid_argument("Malformed OBEX expression");
      }

    } catch (const std::exception &err) {
      std::ostringstream oss;
      oss << "[" << line << "]: " << err.what();
      return std::unexpected(MeterError::custom(EPROTO, oss.str()));
    }
  }

  newDevice.manufacturer = "EasyMeter";
  newDevice.model = "DD3-BZ06-ETA-ODZ1";
  newDevice.fwVersion = std::string(PROJECT_NAME) + " v" + PROJECT_VERSION +
                        " (" + GIT_COMMIT_HASH + ")";
  newDevice.phases = 3;

  // ---- Build ordered JSON ----
  json newJson;

  newJson["manufacturer"] = newDevice.manufacturer;
  newJson["model"] = newDevice.model;
  newJson["serial_number"] = newDevice.serialNumber;
  newJson["firmware_version"] = newDevice.fwVersion;
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
  int reconnectDelay = cfg_.reconnectDelay->min;

  while (handler_.isRunning()) {

    // Connect to meter
    auto connectAction = handleResult(tryConnect());
    if (connectAction == Meter::ErrorAction::SHUTDOWN)
      break;

    else if (connectAction == Meter::ErrorAction::RECONNECT) {
      meterLogger_->warn("Meter disconnected, trying to reconnect in {} {}...",
                         reconnectDelay,
                         reconnectDelay == 1 ? "second" : "seconds");
      {
        std::unique_lock<std::mutex> lock(cbMutex_);
        cv_.wait_for(lock, std::chrono::seconds(reconnectDelay),
                     [this] { return !handler_.isRunning(); });
      }
      if (cfg_.reconnectDelay->exponential && handler_.isRunning())
        reconnectDelay = std::min(reconnectDelay * 2, cfg_.reconnectDelay->max);
      continue;
    } else if (connectAction == Meter::ErrorAction::NONE &&
               cfg_.reconnectDelay->exponential) {
      // Successfully connected - reset reconnect delay
      reconnectDelay = cfg_.reconnectDelay->min;
    }

    // Read telegram - on any error, loop restarts (will try reconnect)
    auto readAction = handleResult(readTelegram());
    if (readAction == Meter::ErrorAction::SHUTDOWN)
      break;
    else if (readAction == Meter::ErrorAction::RECONNECT)
      continue;

    // Update device
    auto deviceAction = handleResult(updateDeviceAndJson());
    if (deviceAction == Meter::ErrorAction::SHUTDOWN)
      break;
    else if (deviceAction == Meter::ErrorAction::RECONNECT)
      continue;

    if (handler_.isRunning()) {
      std::lock_guard<std::mutex> lock(cbMutex_);
      if (deviceCallback_) {
        deviceCallback_(jsonDevice_.dump());
      }
    }

    // Update values
    auto updateAction = handleResult(updateValuesAndJson());
    if (updateAction == Meter::ErrorAction::SHUTDOWN)
      break;
    else if (updateAction == Meter::ErrorAction::RECONNECT)
      continue;

    if (handler_.isRunning()) {
      std::lock_guard<std::mutex> lock(cbMutex_);
      if (updateCallback_) {
        updateCallback_(jsonValues_.dump());
      }
    }
  }

  meterLogger_->debug("Meter run loop stopped.");
}