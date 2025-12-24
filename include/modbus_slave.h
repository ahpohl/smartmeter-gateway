#ifndef MODBUS_SLAVE_H_
#define MODBUS_SLAVE_H_

#include "config_yaml.h"
#include "signal_handler.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

class ModbusSlave {
public:
  ModbusSlave(const ModbusRootConfig &cfg, SignalHandler &signalHandler);
  virtual ~ModbusSlave();

private:
  std::shared_ptr<spdlog::logger> modbusLogger_;
  const ModbusRootConfig &cfg_;

  // --- values and events
  std::vector<uint16_t> regs_;

  // --- threading / callbacks ---
  SignalHandler &handler_;
  mutable std::mutex cbMutex_;
  std::thread worker_;
  std::atomic<bool> connected_{false};
  std::condition_variable cv_;
};

#endif /* MODBUS_SLAVE_H_ */
