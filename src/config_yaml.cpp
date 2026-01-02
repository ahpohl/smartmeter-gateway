#include "config_yaml.h"
#include "meter_types.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

static std::optional<ModbusTcpConfig> parseModbusTcp(const YAML::Node &node) {
  if (!node)
    return std::nullopt;
  ModbusTcpConfig tcp;
  tcp.listen = node["listen"].as<std::string>("0.0.0.0");
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

  // Start with defaults
  rtu.baud = 9600;
  rtu.dataBits = 8;
  rtu.stopBits = 1;
  rtu.parity = MeterTypes::Parity::None;

  // Apply preset if specified
  if (node["preset"]) {
    auto preset = MeterTypes::parsePreset(node["preset"].as<std::string>());
    auto defaults = MeterTypes::getPresetDefaults(preset.value());
    rtu.baud = defaults.baud;
    rtu.dataBits = defaults.dataBits;
    rtu.stopBits = defaults.stopBits;
    rtu.parity = defaults.parity;
  }

  // Apply manual overrides
  if (node["baud"])
    rtu.baud = node["baud"].as<int>();
  if (node["data_bits"])
    rtu.dataBits = node["data_bits"].as<int>();
  if (node["stop_bits"])
    rtu.stopBits = node["stop_bits"].as<int>();
  if (node["parity"])
    rtu.parity = MeterTypes::parseParity(node["parity"].as<std::string>());

  // Validate
  if (rtu.baud <= 0)
    throw std::invalid_argument("modbus.rtu.baud must be positive");
  if (rtu.dataBits < 5 || rtu.dataBits > 8)
    throw std::invalid_argument("modbus.rtu.data_bits must be between 5 and 8");
  if (!(rtu.stopBits == 1 || rtu.stopBits == 2))
    throw std::invalid_argument("modbus.rtu.stop_bits must be 1 or 2");

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

static std::optional<GridConfig> parseGrid(const YAML::Node &node) {
  if (!node)
    return std::nullopt;

  GridConfig cfg;
  cfg.powerFactor = node["power_factor"].as<double>(0.95);
  cfg.frequency = node["frequency"].as<double>(50.0);

  // Validate
  if (cfg.powerFactor <= -1.0 || cfg.powerFactor >= 1.0)
    throw std::invalid_argument(
        "meter. grid.power_factor must be in range (-1.0, 1.0]");
  if (cfg.frequency <= 0.0)
    throw std::invalid_argument("meter.grid.frequency must be positive");

  return cfg;
}

static MeterConfig parseMeter(const YAML::Node &node) {
  if (!node)
    throw std::runtime_error("Missing 'meter' section in config");

  MeterConfig cfg;
  cfg.device = node["device"].as<std::string>("/dev/ttyUSB0");

  // Start with defaults
  cfg.baud = 9600;
  cfg.dataBits = 8;
  cfg.stopBits = 1;
  cfg.parity = MeterTypes::Parity::None;

  // Apply preset if specified
  if (node["preset"]) {
    auto preset = MeterTypes::parsePreset(node["preset"].as<std::string>());
    auto defaults = MeterTypes::getPresetDefaults(preset.value());
    cfg.baud = defaults.baud;
    cfg.dataBits = defaults.dataBits;
    cfg.stopBits = defaults.stopBits;
    cfg.parity = defaults.parity;
  }

  // Apply manual overrides
  if (node["baud"])
    cfg.baud = node["baud"].as<int>();
  if (node["data_bits"])
    cfg.dataBits = node["data_bits"].as<int>();
  if (node["stop_bits"])
    cfg.stopBits = node["stop_bits"].as<int>();
  if (node["parity"])
    cfg.parity = MeterTypes::parseParity(node["parity"].as<std::string>());

  // Parse optional grid parameters
  if (node["grid"])
    cfg.grid = parseGrid(node["grid"]);

  // Validate
  if (cfg.baud <= 0)
    throw std::invalid_argument("meter.baud must be positive");
  if (cfg.dataBits < 5 || cfg.dataBits > 8)
    throw std::invalid_argument("meter.data_bits must be between 5 and 8");
  if (!(cfg.stopBits == 1 || cfg.stopBits == 2))
    throw std::invalid_argument("meter.stop_bits must be 1 or 2");

  return cfg;
}

static std::optional<ModbusRootConfig> parseModbus(const YAML::Node &node) {
  if (!node)
    return std::nullopt;

  ModbusRootConfig cfg;

  // --- Subsections ---
  cfg.tcp = parseModbusTcp(node["tcp"]);
  cfg.rtu = parseModbusRtu(node["rtu"]);

  if (!cfg.tcp && !cfg.rtu)
    throw std::runtime_error(
        "Config must specify at least one of 'modbus.tcp' or 'modbus.rtu'");
  if (cfg.tcp && cfg.rtu)
    cfg.rtu = std::nullopt; // TCP takes priority

  // MANDATORY boolean: use_float_model
  if (!node["use_float_model"]) {
    throw std::runtime_error(
        "Missing mandatory 'modbus.use_float_model' key in config");
  }
  cfg.useFloatModel = node["use_float_model"].as<bool>();

  // --- Basic parameters ---
  cfg.slaveId = node["slave_id"].as<int>(1);
  cfg.requestTimeout = node["request_timeout"].as<int>(5);
  cfg.idleTimeout = node["idle_timeout"].as<int>(60);

  // --- Validation ---
  if (cfg.slaveId < 1 || cfg.slaveId > 247)
    throw std::invalid_argument("modbus.slave_id must be in range 1–247");

  if (cfg.requestTimeout <= 0)
    throw std::invalid_argument("modbus.request_timeout must be positive");

  if (cfg.idleTimeout <= 0)
    throw std::invalid_argument("modbus.idle_timeout must be positive");

  if (cfg.idleTimeout < cfg.requestTimeout)
    throw std::invalid_argument(
        "modbus.idle_timeout must be >= request_timeout");

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
  cfg.meter = parseMeter(root["meter"]);

  return cfg;
}