#ifndef LOGGER_H_
#define LOGGER_H_

#include "config_yaml.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

inline void setupLogging(const LoggerConfig &cfg) {
  // single console sink
  auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

  // default/global logger
  auto defaultLogger = std::make_shared<spdlog::logger>("", sink);
  defaultLogger->set_level(cfg.globalLevel);
  spdlog::set_default_logger(defaultLogger);

  // per-module loggers
  for (const auto &kv : cfg.moduleLevels) {
    auto logger = std::make_shared<spdlog::logger>(kv.first, sink);
    logger->set_level(kv.second);
    spdlog::register_logger(logger);
  }

  // simple pattern
  spdlog::set_pattern("[%n] [%^%l%$] %v");
}

#endif /* LOGGER_H_ */
