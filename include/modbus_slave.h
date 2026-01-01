#ifndef MODBUS_SLAVE_H_
#define MODBUS_SLAVE_H_

#include "config_yaml.h"
#include "meter_types.h"
#include "modbus_error.h"
#include "signal_handler.h"
#include <atomic>
#include <expected>
#include <memory>
#include <modbus/modbus.h>
#include <mutex>
#include <poll.h>
#include <thread>

class ModbusSlave {
public:
  ModbusSlave(const ModbusRootConfig &cfg, SignalHandler &signalHandler);
  virtual ~ModbusSlave();
  void updateValues(MeterTypes::Values values);
  void updateDevice(MeterTypes::Device device);
  static constexpr int MODBUS_REGISTERS = 65535;

private:
  std::shared_ptr<spdlog::logger> modbusLogger_;
  const ModbusRootConfig &cfg_;
  MeterTypes::ErrorAction
  handleResult(std::expected<void, ModbusError> &&result);

  // RTU and TCP listener and connection handler
  modbus_t *listenCtx_{nullptr};
  int serverSocket_{-1};
  std::expected<void, ModbusError> startListener(void);
  void rtuClientHandler(void);
  void tcpClientHandler();
  void tcpClientWorker(int clientSocket);

  // --- modbus registers and values
  std::atomic<std::shared_ptr<modbus_mapping_t>> regs_{nullptr};
  struct ModbusDeleter {
    void operator()(modbus_mapping_t *p) {
      if (p)
        modbus_mapping_free(p);
    }
  };
  bool deviceUpdated_{false};

  // --- signals / threading / callbacks ---
  SignalHandler &handler_;
  mutable std::mutex clientMutex_;
  std::thread worker_;
  std::vector<std::thread> clientThreads_;
};

#endif /* MODBUS_SLAVE_H_ */
