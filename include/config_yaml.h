#ifndef CONFIG_YAML_HPP
#define CONFIG_YAML_HPP

#include "meter_types.h"
#include <map>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

// --- Modbus TCP config ---
struct ModbusTcpConfig {
  std::string listen;
  int port;
};

// --- Modbus RTU config ---
struct ModbusRtuConfig {
  std::string device;
  int baud;
  int dataBits;
  int stopBits;
  MeterTypes::Parity parity;
};

// --- MQTT reconnect delay config ---
struct ReconnectDelayConfig {
  int min;
  int max;
  bool exponential;
};

// --- Grid config ---
struct GridConfig {
  double powerFactor;
  double frequency;
};

// Meter config
struct MeterConfig {
  std::string device;
  int baud;
  int dataBits;
  int stopBits;
  MeterTypes::Parity parity;
  std::optional<GridConfig> grid;
};

// --- Root Modbus config ---
struct ModbusRootConfig {
  std::optional<ModbusTcpConfig> tcp;
  std::optional<ModbusRtuConfig> rtu;

  int slaveId{1};
  int requestTimeout;
  int idleTimeout;
  bool useFloatModel;
};

// MQTT config
struct MqttConfig {
  std::string broker;
  int port;
  std::string topic;
  std::optional<std::string> user;
  std::optional<std::string> password;
  size_t queueSize;

  // Optional retry parameters
  std::optional<ReconnectDelayConfig> reconnectDelay;
};

// --- Logger config ---
struct LoggerConfig {
  spdlog::level::level_enum globalLevel = spdlog::level::info;
  std::map<std::string, spdlog::level::level_enum> moduleLevels;
};

// Root config
struct Config {
  MeterConfig meter;
  MqttConfig mqtt;
  LoggerConfig logger;
  std::optional<ModbusRootConfig> modbus;
};

// Forward declaration
Config loadConfig(const std::string &path);

// Small helper to convert std::optional<std::string> → const char*
inline const char *opt_c_str(const std::optional<std::string> &s) {
  return s ? s->c_str() : nullptr;
}

#endif
