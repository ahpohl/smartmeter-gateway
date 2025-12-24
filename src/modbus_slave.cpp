#include "modbus_slave.h"
#include "signal_handler.h"

ModbusSlave::ModbusSlave(const ModbusRootConfig &cfg,
                         SignalHandler &signalHandler)
    : cfg_(cfg), handler_(signalHandler) {

  modbusLogger_ = spdlog::get("modbus");
  if (!modbusLogger_)
    modbusLogger_ = spdlog::default_logger();

  regs_.resize(0xFFFF, 0);
}

ModbusSlave::~ModbusSlave() {
  cv_.notify_all();
  if (worker_.joinable())
    worker_.join();

  modbusLogger_->info("Modbus slave stopped");
}
