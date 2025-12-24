#ifndef MODBUS_SLAVE_H_
#define MODBUS_SLAVE_H_

#include "config_yaml.h"
#include "modbus_error.h"
#include "signal_handler.h"
#include <atomic>
#include <condition_variable>
#include <expected>
#include <modbus/modbus.h>
#include <mutex>
#include <thread>

class ModbusSlave {
public:
  ModbusSlave(const ModbusRootConfig &cfg, SignalHandler &signalHandler);
  virtual ~ModbusSlave();

private:
  void runLoop();
  std::expected<void, ModbusError> startListener(void);
  std::shared_ptr<spdlog::logger> modbusLogger_;
  const ModbusRootConfig &cfg_;

  // --- modbus registers and values
  modbus_t *ctx_{nullptr};
  modbus_mapping_t *regs_{nullptr};

  // --- threading / callbacks ---
  SignalHandler &handler_;
  mutable std::mutex cbMutex_;
  std::thread worker_;
  std::atomic<bool> connected_{false};
  std::condition_variable cv_;
};

#endif /* MODBUS_SLAVE_H_ */
