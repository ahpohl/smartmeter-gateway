#include "config_yaml.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

static std::optional<ModbusTcpConfig> parseModbusTcp(const YAML::Node &node) {
  if (!node)
    return std::nullopt;
  ModbusTcpConfig tcp;
  tcp.host = node["host"].as<std::string>("localhost");
  tcp.port = node["port"].as<int>(502);

  if (tcp.port <= 0 || tcp.port > 65535)
    throw std::invalid_argument("Modbus TCP port must be in range 1–65535");

  return tcp;
}

static std::optional<ModbusRtuConfig> parseModbusRtu(const YAML::Node &node) {
  if (!node)
    return std::nullopt;
  ModbusRtuConfig rtu;
  rtu.device = node["device"].as<std::string>("/dev/ttyUSB0");
  rtu.baud = node["baud"].as<int>(9600);

  if (rtu.baud <= 0)
    throw std::invalid_argument("Modbus RTU baud must be positive");

  return rtu;
}

static std::optional<ReconnectDelayConfig>
parseReconnectDelay(const YAML::Node &node) {
  if (!node)
    return std::nullopt;

  ReconnectDelayConfig cfg;
  cfg.min = node["min"].as<int>(5);
  cfg.max = node["max"].as<int>(365);
  cfg.exponential = node["exponential"].as<bool>(true);

  if (cfg.min <= 0)
    throw std::invalid_argument("reconnect_delay.min must be positive");
  if (cfg.max <= 0)
    throw std::invalid_argument("reconnect_delay.max must be positive");
  if (cfg.min >= cfg.max)
    throw std::invalid_argument("reconnect_delay.min must be smaller than max");

  return cfg;
}

static std::optional<ResponseTimeoutConfig>
parseResponseTimeout(const YAML::Node &node) {
  if (!node)
    return std::nullopt;

  ResponseTimeoutConfig cfg;
  cfg.sec = node["sec"].as<int>(0);
  cfg.usec = node["usec"].as<int>(200000);

  if (cfg.sec == 0 && cfg.usec == 0) {
    throw std::invalid_argument(
        "Both response timeout sec and usec cannot be 0");
  }
  if (cfg.sec < 0 || cfg.usec > 999999) {
    throw std::invalid_argument(
        "Response timeout usec must be in range 0-999999");
  }

  return cfg;
}

static ModbusRootConfig parseModbus(const YAML::Node &node) {
  if (!node)
    throw std::runtime_error("Missing 'modbus' section in config");

  ModbusRootConfig cfg;

  // --- Subsections ---
  cfg.tcp = parseModbusTcp(node["tcp"]);
  cfg.rtu = parseModbusRtu(node["rtu"]);

  if (!cfg.tcp && !cfg.rtu)
    throw std::runtime_error(
        "Config must specify at least one of 'modbus.tcp' or 'modbus.rtu'");
  if (cfg.tcp && cfg.rtu)
    cfg.rtu = std::nullopt; // TCP takes priority

  // --- Basic parameters ---
  cfg.slaveId = node["slave_id"].as<int>(1);
  cfg.updateInterval = node["update_interval"].as<int>(5);
  cfg.timeout = node["response_timeout"].as<int>(1);

  // --- Optional ---
  if (node["reconnect_delay"])
    cfg.reconnectDelay = parseReconnectDelay(node["reconnect_delay"]);
  if (node["response_timeout"])
    cfg.responseTimeout = parseResponseTimeout(node["response_timeout"]);

  // --- Validation ---
  if (cfg.slaveId < 1 || cfg.slaveId > 247)
    throw std::invalid_argument("Modbus slave_id must be in range 1–247");

  if (cfg.updateInterval <= 0)
    throw std::invalid_argument("modbus.update_interval must be positive");

  return cfg;
}

static MqttConfig parseMqtt(const YAML::Node &node) {
  if (!node)
    throw std::runtime_error("Missing 'mqtt' section in config");

  if (!node["topic"])
    throw std::runtime_error("Missing required field: mqtt.topic");

  MqttConfig cfg;

  // --- Basic parameters ---
  cfg.broker = node["broker"].as<std::string>("localhost");
  cfg.port = node["port"].as<int>(1883);
  cfg.topic = node["topic"].as<std::string>();
  cfg.queueSize = node["queue_size"].as<size_t>(1000);

  // --- Optional credentials ---
  if (node["user"])
    cfg.user = node["user"].as<std::string>();
  if (node["password"])
    cfg.password = node["password"].as<std::string>();

  // --- Optional reconnect delay ---
  if (node["reconnect_delay"])
    cfg.reconnectDelay = parseReconnectDelay(node["reconnect_delay"]);

  // --- Validation ---
  if (cfg.port <= 0 || cfg.port > 65535)
    throw std::invalid_argument("mqtt.port must be in range 1–65535");

  if (cfg.queueSize == 0)
    throw std::invalid_argument("mqtt.queue_size must be greater than zero");

  return cfg;
}

static spdlog::level::level_enum parseLogLevel(const std::string &s) {
  if (s == "off")
    return spdlog::level::off;
  if (s == "error")
    return spdlog::level::err;
  if (s == "warn")
    return spdlog::level::warn;
  if (s == "info")
    return spdlog::level::info;
  if (s == "debug")
    return spdlog::level::debug;
  if (s == "trace")
    return spdlog::level::trace;
  return spdlog::level::info;
}

static LoggerConfig parseLogger(const YAML::Node &node) {
  LoggerConfig cfg;
  if (!node)
    return cfg;

  if (node["level"])
    cfg.globalLevel = parseLogLevel(node["level"].as<std::string>());

  if (node["modules"]) {
    for (auto it : node["modules"]) {
      std::string module = it.first.as<std::string>();
      cfg.moduleLevels[module] = parseLogLevel(it.second.as<std::string>());
    }
  }
  return cfg;
}

Config loadConfig(const std::string &path) {
  YAML::Node root = YAML::LoadFile(path);
  Config cfg;

  cfg.modbus = parseModbus(root["modbus"]);
  cfg.mqtt = parseMqtt(root["mqtt"]);
  cfg.logger = parseLogger(root["logger"]);

  return cfg;
}
