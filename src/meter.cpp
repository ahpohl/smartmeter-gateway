#include "meter.h"
#include "config_yaml.h"
#include "json_utils.h"
#include "meter_error.h"
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

/*
telegram_ = R"(/EBZ5DD3BZ06ETA_107

1-0:0.0.0*255(1EBZ0100507409)
1-0:96.1.0*255(1EBZ0100507409)
1-0:1.8.0*255(000125.25688570*kWh)
1-0:16.7.0*255(000259.20*W)
1-0:36.7.0*255(000075.18*W)
1-0:56.7.0*255(000092.34*W)
1-0:76.7.0*255(000091.68*W)
1-0:32.7.0*255(232.4*V)
1-0:52.7.0*255(231.7*V)
1-0:72.7.0*255(233.7*V)
1-0:96.5.0*255(001C0104)
0-0:96.8.0*255(00104443)
!)";
*/

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
    }
  }
  meterLogger_->info("Meter disconnected");
}

void Meter::setUpdateCallback(std::function<void(const std::string &)> cb) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  updateCallback_ = std::move(cb);
}

Meter::ErrorAction
Meter::handleResult(std::expected<void, MeterError> &&result) {
  if (result) {
    return Meter::ErrorAction::NONE;
  }

  const MeterError &err = result.error();

  if (err.severity == MeterError::Severity::FATAL) {
    meterLogger_->error("FATAL Meter error: {}", err.describe());
    handler_.shutdown();
    return Meter::ErrorAction::SHUTDOWN;

  } else if (err.severity == MeterError::Severity::TRANSIENT) {
    meterLogger_->debug("Transient Meter error: {}", err.describe());
    disconnect();
    return Meter::ErrorAction::RECONNECT;
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

  // await a full 64-byte block (VMIN=64) with 0.5s inter-byte timeout
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

  return {};
}

std::expected<void, MeterError> Meter::readTelegram() {
  if (!handler_.isRunning()) {
    return std::unexpected(
        MeterError::custom(EINTR, "readTelegram(): Shutdown in progress"));
  }

  if (serialPort_ == -1)
    return std::unexpected(
        MeterError::fromErrno("readTelegram(): Meter not connected"));

  std::vector<char> buffer(BUFFER_SIZE);
  size_t bufPos = 0;
  ssize_t bytesReceived = 0;

  std::vector<char> packet(TELEGRAM_SIZE);
  size_t packetPos = 0;
  bool messageBegin = false;

  while (packetPos < TELEGRAM_SIZE) {
    if (bufPos >= static_cast<std::size_t>(bytesReceived)) {
      std::fill(buffer.begin(), buffer.end(), '\0');

      bytesReceived = ::read(serialPort_, buffer.data(), BUFFER_SIZE);
      if (bytesReceived == -1) {
        return std::unexpected(
            MeterError::fromErrno("Failed to read serial device"));
      }
      if (bytesReceived == 0) {
        return std::unexpected(
            MeterError::custom(EOF, "readTelegram(): serial device closed"));
      }
      bufPos = 0;
    }

    // consume one char from the buffer
    char c = buffer[bufPos++];
    if (c == '/')
      messageBegin = true;

    if (messageBegin) {
      packet[packetPos++] = c;
    }
  }

  // Ensure we have at least 3 bytes and the third-from-last is '!'
  if (packetPos < 3 || packet[packetPos - 3] != '!') {
    return std::unexpected(MeterError::custom(
        EPROTO, "readTelegram(): telegram stream not in sync"));
  }

  meterLogger_->trace("Received telegram (len {}):\n{}", packetPos,
                      std::string(packet.begin(), packet.end()));

  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    telegram_.assign(packet.begin(), packet.end());
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

  Values values{};

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

  // Update shared values and JSON with lock
  {
    std::lock_guard<std::mutex> lock(cbMutex_);
    values_ = std::move(values);
    json_ = std::move(newJson);
  }

  meterLogger_->debug("{}", json_.dump());

  return {};
}

void Meter::runLoop() {
  int reconnectDelay = cfg_.reconnectDelay->min;

  while (handler_.isRunning()) {

    // Connect to meter
    auto connectAction = handleResult(tryConnect());
    if (connectAction == Meter::ErrorAction::SHUTDOWN)
      break;

    if (connectAction == Meter::ErrorAction::RECONNECT) {
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
    if (handleResult(readTelegram()) != Meter::ErrorAction::NONE)
      break;

    // Update values
    if (handleResult(updateValuesAndJson()) != Meter::ErrorAction::NONE)
      break;

    // Activate callback
    {
      std::lock_guard<std::mutex> lock(cbMutex_);
      if (updateCallback_) {
        updateCallback_(json_.dump());
      }
    }
  }

  /*
      // --- Wait for next update interval ---
      std::unique_lock<std::mutex> lock(cbMutex_);
      cv_.wait_for(lock, std::chrono::seconds(cfg_.updateInterval),
                   [this] { return !handler_.isRunning(); });
  */
  meterLogger_->debug("Meter run loop stopped.");
}