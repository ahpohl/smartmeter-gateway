#ifndef CONFIG_YAML_HPP
#define CONFIG_YAML_HPP

#include <map>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

// --- Meter config ---
enum class SerialParity { None, Even, Odd };

// --- Modbus TCP config ---
struct ModbusTcpConfig {
  std::string host;
  int port;
};

// --- Modbus RTU config ---
struct ModbusRtuConfig {
  std::string device;
  int baud;
};

// --- Response timeout config ---
struct ResponseTimeoutConfig {
  int sec{0};
  int usec{200000};
};

// --- Reconnect delay config ---
struct ReconnectDelayConfig {
  int min{5};
  int max{320};
  bool exponential{true};
};

// Meter config
struct MeterConfig {
  std::string device;
  int baud;
  int dataBits;
  int stopBits;
  SerialParity parity;

  // Optional retry parameters
  std::optional<ReconnectDelayConfig> reconnectDelay;
};

// --- Root Modbus config ---
struct ModbusRootConfig {
  std::optional<ModbusTcpConfig> tcp;
  std::optional<ModbusRtuConfig> rtu;

  int slaveId{1};
  int timeout{1};

  // Optional parameters
  std::optional<ResponseTimeoutConfig> responseTimeout;
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
  ModbusRootConfig modbus;
  MqttConfig mqtt;
  LoggerConfig logger;
  MeterConfig meter;
};

// Forward declaration
Config loadConfig(const std::string &path);

// Small helper to convert std::optional<std::string> → const char*
inline const char *opt_c_str(const std::optional<std::string> &s) {
  return s ? s->c_str() : nullptr;
}

#endif
