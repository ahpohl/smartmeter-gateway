#ifndef METER_H_
#define METER_H_

#include "config_yaml.h"
#include "meter_error.h"
#include "meter_types.h"
#include "signal_handler.h"
#include <condition_variable>
#include <expected>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>
#include <string>
#include <thread>

class Meter {
public:
  explicit Meter(const MeterConfig &cfg, SignalHandler &signalHandler);
  virtual ~Meter();

  std::string getJsonDump(void) const;
  MeterTypes::Values getValues(void) const;
  void setUpdateCallback(
      std::function<void(const std::string &, const MeterTypes::Values &)> cb);
  void setDeviceCallback(
      std::function<void(const std::string &, const MeterTypes::Device &)> cb);
  void setAvailabilityCallback(std::function<void(const std::string &)> cb);

  static constexpr size_t BUFFER_SIZE = 64;
  static constexpr size_t TELEGRAM_SIZE = 368;

private:
  void runLoop();
  enum class ErrorAction { NONE, RECONNECT, SHUTDOWN };
  Meter::ErrorAction handleResult(std::expected<void, MeterError> &&result);
  void disconnect(void);
  std::expected<void, MeterError> updateValuesAndJson(void);
  std::expected<void, MeterError> updateDeviceAndJson(void);
  std::expected<void, MeterError> tryConnect(void);
  std::expected<void, MeterError> readTelegram(void);

  const MeterConfig &cfg_;
  MeterTypes::Values values_;
  MeterTypes::Device device_;
  std::string telegram_;
  nlohmann::ordered_json jsonValues_;
  nlohmann::json jsonDevice_;
  std::shared_ptr<spdlog::logger> meterLogger_;
  int serialPort_{-1};

  // --- threading / callbacks ---
  std::function<void(const std::string &, const MeterTypes::Values &)>
      updateCallback_;
  std::function<void(const std::string &, const MeterTypes::Device &)>
      deviceCallback_;
  std::function<void(const std::string &)> availabilityCallback_;
  SignalHandler &handler_;
  mutable std::mutex cbMutex_;
  std::condition_variable cv_;
  std::thread worker_;
  std::thread dongle_;
};

#endif /* METER_H_ */
