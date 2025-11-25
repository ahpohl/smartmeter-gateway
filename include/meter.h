#ifndef METER_H_
#define METER_H_

#include "config_yaml.h"
#include "signal_handler.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>
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

private:
  void runLoop();

  const MeterConfig &cfg_;
  Values values_;
  nlohmann::ordered_json json_;
  std::shared_ptr<spdlog::logger> meterLogger_;

  // --- threading / callbacks ---
  std::function<void(const nlohmann::ordered_json &)> updateCallback_;
  SignalHandler &handler_;
  mutable std::mutex cbMutex_;
  std::thread worker_;
  std::condition_variable cv_;
  std::atomic<bool> connected_{false};
};

#endif /* METER_H_ */
