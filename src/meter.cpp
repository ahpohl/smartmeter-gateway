#include "meter.h"
#include "config_yaml.h"
#include "signal_handler.h"

Meter::Meter(const MeterConfig &cfg, SignalHandler &signalHandler)
    : cfg_(cfg), handler_(signalHandler) {

  meterLogger_ = spdlog::get("meter");
  if (!meterLogger_)
    meterLogger_ = spdlog::default_logger();
}

Meter::~Meter() {}
