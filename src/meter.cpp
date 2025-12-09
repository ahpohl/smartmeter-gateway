#include "meter.h"
#include "config_yaml.h"
#include "json_utils.h"
#include "meter_error.h"
#include "signal_handler.h"
#include <chrono>
#include <expected>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <string>

using json = nlohmann::ordered_json;

Meter::Meter(const MeterConfig &cfg, SignalHandler &signalHandler)
    : cfg_(cfg), handler_(signalHandler) {

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

  meterLogger_ = spdlog::get("meter");
  if (!meterLogger_)
    meterLogger_ = spdlog::default_logger();

  // Start update loop thread
  worker_ = std::thread(&Meter::runLoop, this);
}

Meter::~Meter() {}

std::expected<void, MeterError> Meter::updateValuesAndJson() {

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
      return std::unexpected(MeterError::custom(EINVAL, oss.str()));
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
  while (handler_.isRunning()) {

    auto update = updateValuesAndJson();
    if (!update) {
      errorHandler(update.error());
    } else {
      std::lock_guard<std::mutex> lock(cbMutex_);
      if (updateCallback_) {
        try {
          updateCallback_(json_.dump());
        } catch (const std::exception &ex) {
          meterLogger_->error("FATAL error in Meter update callback: {}",
                              ex.what());
          handler_.shutdown();
        }
      }
    }

    std::unique_lock<std::mutex> lock(cbMutex_);
    cv_.wait_for(lock, std::chrono::seconds(cfg_.updateInterval),
                 [this] { return !handler_.isRunning(); });
  }

  meterLogger_->debug("Meter run loop stopped.");
}

void Meter::setUpdateCallback(std::function<void(const std::string &)> cb) {
  std::lock_guard<std::mutex> lock(cbMutex_);
  updateCallback_ = std::move(cb);
}

void Meter::errorHandler(const MeterError &err) {
  if (err.severity == MeterError::Severity::FATAL) {
    meterLogger_->error("FATAL Meter error: {}", err.describe());

    // FATAL error: terminate main loop
    handler_.shutdown();

  } else if (err.severity == MeterError::Severity::TRANSIENT) {
    meterLogger_->debug("Transient Meter error: {}", err.describe());
  }
}