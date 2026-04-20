#ifndef CONFIG_YAML_HPP
#define CONFIG_YAML_HPP

#include <map>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <termios.h>

// ---------------------------------------------------------------------------
// Serial types
// ---------------------------------------------------------------------------

enum class Parity { None, Even, Odd };

// ---------------------------------------------------------------------------
// Conversion helpers (defined in config_yaml.cpp)
// ---------------------------------------------------------------------------

char parityToChar(Parity parity);
speed_t baudToSpeed(int baud);
tcflag_t dataBitsToFlag(int dataBits);
void applyParity(termios &tty, Parity parity);

// ---------------------------------------------------------------------------
// Modbus transport configs
// ---------------------------------------------------------------------------

struct ModbusTcpClientConfig {
  std::string host;
  int port{502};
};

struct ModbusTcpServerConfig {
  std::string listen{"0.0.0.0"};
  int port{502};
};

struct ModbusRtuConfig {
  std::string device;
  int baud{9600};
  int dataBits{8};
  int stopBits{1};
  Parity parity{Parity::None};
};

// ---------------------------------------------------------------------------
// Shared sub-configs
// ---------------------------------------------------------------------------

struct ReconnectDelayConfig {
  int min{5};
  int max{320};
  bool exponential{true};
};

struct ResponseTimeoutConfig {
  int sec{5};
  int usec{0};
};

// ---------------------------------------------------------------------------
// Meter configs
// ---------------------------------------------------------------------------

// --- Grid config ---
struct GridConfig {
  double powerFactor{0.95};
  double frequency{50.0};
  bool isLeading{false};
};

struct MeterMasterConfig {
  std::optional<ModbusTcpClientConfig> tcp;
  std::optional<ModbusRtuConfig> rtu;
  int slaveId{1};
  GridConfig grid;
};

struct MeterSlaveConfig {
  std::optional<ModbusTcpServerConfig> tcp;
  std::optional<ModbusRtuConfig> rtu;
  int slaveId{1};
  int requestTimeout{5};
  int idleTimeout{60};
  bool useFloatModel{false};
};

struct MeterConfig {
  MeterMasterConfig master;
  std::optional<MeterSlaveConfig> slave;
};

// ---------------------------------------------------------------------------
// MQTT config
// ---------------------------------------------------------------------------

struct MqttConfig {
  std::string broker{"localhost"};
  int port{1883};
  std::string topic;
  std::optional<std::string> user;
  std::optional<std::string> password;
  size_t queueSize{100};
  ReconnectDelayConfig reconnectDelay;
};

// ---------------------------------------------------------------------------
// Logger config
// ---------------------------------------------------------------------------

struct LoggerConfig {
  spdlog::level::level_enum globalLevel{spdlog::level::info};
  std::map<std::string, spdlog::level::level_enum> moduleLevels;
};

// ---------------------------------------------------------------------------
// Root config
// ---------------------------------------------------------------------------

struct AppConfig {
  MeterConfig meter;
  MqttConfig mqtt;
  LoggerConfig logger;
};

AppConfig loadConfig(const std::string &path);

inline const char *opt_c_str(const std::optional<std::string> &s) {
  return s ? s->c_str() : nullptr;
}

#endif