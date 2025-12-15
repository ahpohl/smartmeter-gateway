#ifndef METER_H_
#define METER_H_

#include "config_yaml.h"
#include "meter_error.h"
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

  struct Phase {
    double voltage{0.0};
    double power{0.0};
  };

  struct Values {
    uint64_t time{0};
    double energy{0.0};
    double power{0.0};
    Phase phase1;
    Phase phase2;
    Phase phase3;
  };

  std::string getJsonDump(void) const;
  Values getValues(void) const;
  void setUpdateCallback(std::function<void(const std::string &)> cb);

  static constexpr size_t BUFFER_SIZE = 64;
  static constexpr size_t TELEGRAM_SIZE = 368;

private:
  void runLoop();
  void errorHandler(const MeterError &err);
  void disconnect(void);
  std::expected<void, MeterError> updateValuesAndJson(void);
  std::expected<void, MeterError> tryConnect(void);
  std::expected<void, MeterError> readTelegram(void);

  const MeterConfig &cfg_;
  Values values_;
  std::string telegram_;
  nlohmann::ordered_json json_;
  std::shared_ptr<spdlog::logger> meterLogger_;
  int serialPort_{-1};

  // --- threading / callbacks ---
  std::function<void(const nlohmann::ordered_json &)> updateCallback_;
  SignalHandler &handler_;
  mutable std::mutex cbMutex_;
  std::condition_variable cv_;
  std::thread worker_;
  std::thread dongle_;
};

#endif /* METER_H_ */
