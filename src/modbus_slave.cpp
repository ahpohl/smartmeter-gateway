#include "modbus_slave.h"
#include "modbus_error.h"
#include "signal_handler.h"
#include <expected>
#include <modbus/modbus.h>

ModbusSlave::ModbusSlave(const ModbusRootConfig &cfg,
                         SignalHandler &signalHandler)
    : cfg_(cfg), handler_(signalHandler) {

  modbusLogger_ = spdlog::get("modbus");
  if (!modbusLogger_)
    modbusLogger_ = spdlog::default_logger();
}

ModbusSlave::~ModbusSlave() {
  cv_.notify_all();
  if (worker_.joinable())
    worker_.join();

  modbus_mapping_free(regs_);
  modbus_free(ctx_);

  modbusLogger_->info("Modbus slave stopped");
}

std::expected<void, ModbusError> ModbusSlave::startListener(void) { return {}; }
