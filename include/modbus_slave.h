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

private:
  void runLoop();
  std::expected<void, ModbusError> startListener(void);
  std::shared_ptr<spdlog::logger> modbusLogger_;
  const ModbusRootConfig &cfg_;
  MeterTypes::ErrorAction
  handleResult(std::expected<void, ModbusError> &&result);
  void clientWorker(int clientSocket);
  std::expected<void, ModbusError> rtuModbusMaster(void);

  // --- modbus registers and values
  modbus_t *ctx_{nullptr};
  std::atomic<std::shared_ptr<modbus_mapping_t>> regs_{nullptr};
  struct ModbusDeleter {
    void operator()(modbus_mapping_t *p) {
      if (p)
        modbus_mapping_free(p);
    }
  };
  int serverSocket_{-1};

  // --- threading / callbacks ---
  SignalHandler &handler_;
  mutable std::mutex mbMutex_;
  std::thread worker_;
  std::vector<std::thread> clientThreads_;
  int sfd_{-1};
  struct pollfd fds_[2];
};

#endif /* MODBUS_SLAVE_H_ */
