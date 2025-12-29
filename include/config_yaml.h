#ifndef CONFIG_YAML_HPP
#define CONFIG_YAML_HPP

#include <map>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

// --- Meter parity config ---
enum class SerialParity { None, Even, Odd };

// --- Meter preset types ---
enum class MeterPreset {
  OdType, // Optical interface: 9600 7E1
  SdType  // Multi functional interface: 9600 8N1
};

// --- Modbus TCP config ---
struct ModbusTcpConfig {
  std::string listen;
  int port;
};

// --- Modbus RTU config ---
struct ModbusRtuConfig {
  std::string device;
  int baud;
};

// --- MQTT reconnect delay config ---
struct ReconnectDelayConfig {
  int min;
  int max;
  bool exponential;
};

// Meter config
struct MeterConfig {
  std::string device;
  int reconnectDelay;

  // Preset configuration (optional)
  std::optional<MeterPreset> preset;

  // Manual overrides (optional when preset is used)
  std::optional<int> baud;
  std::optional<int> dataBits;
  std::optional<int> stopBits;
  std::optional<SerialParity> parity;

  // Resolved serial parameters
  struct SerialParams {
    int baud;
    int dataBits;
    int stopBits;
    SerialParity parity;
  };

  // Get all resolved parameters at once (recommended)
  SerialParams getSerialParams() const;
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
