#include "config_yaml.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

static SerialParity parseParity(const std::string &val) {
  if (val == "none")
    return SerialParity::None;
  if (val == "even")
    return SerialParity::Even;
  if (val == "odd")
    return SerialParity::Odd;
  throw std::invalid_argument("meter.parity must be one of: none, even, odd");
}

static std::optional<MeterPreset> parsePreset(const std::string &val) {
  if (val == "od_type")
    return MeterPreset::OdType;
  if (val == "sd_type")
    return MeterPreset::SdType;
  throw std::invalid_argument("meter.preset must be one of: od_type, sd_type");
}

// Helper function to get preset defaults
static void applyPresetDefaults(MeterPreset preset, int &baud, int &dataBits,
                                int &stopBits, SerialParity &parity) {
  switch (preset) {
  case MeterPreset::OdType:
    baud = 9600;
    dataBits = 7;
    stopBits = 1;
    parity = SerialParity::Even;
    break;
  case MeterPreset::SdType:
    baud = 9600;
    dataBits = 8;
    stopBits = 1;
    parity = SerialParity::None;
    break;
  }
}

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

static MeterConfig parseMeter(const YAML::Node &node) {
  if (!node)
    throw std::runtime_error("Missing 'meter' section in config");

  MeterConfig cfg;
  cfg.device = node["device"].as<std::string>("/dev/ttyUSB0");
  cfg.reconnectDelay = node["reconnect_delay"].as<int>(5);

  // Parse optional preset
  if (node["preset"]) {
    cfg.preset = parsePreset(node["preset"].as<std::string>());
  }

  // Parse optional manual overrides
  if (node["baud"])
    cfg.baud = node["baud"].as<int>();
  if (node["data_bits"])
    cfg.dataBits = node["data_bits"].as<int>();
  if (node["stop_bits"])
    cfg.stopBits = node["stop_bits"].as<int>();
  if (node["parity"])
    cfg.parity = parseParity(node["parity"].as<std::string>());

  // Validate reconnect delay
  if (cfg.reconnectDelay <= 0)
    throw std::invalid_argument("meter.reconnect_delay must be positive");

  // Validate: either preset or all manual parameters must be provided
  bool hasManual = cfg.baud.has_value() && cfg.dataBits.has_value() &&
                   cfg.stopBits.has_value() && cfg.parity.has_value();

  if (!cfg.preset.has_value() && !hasManual) {
    throw std::invalid_argument(
        "meter config must specify either 'preset' or all "
        "of 'baud', 'data_bits', 'stop_bits', 'parity'");
  }

  // Validate effective values
  if (cfg.getSerialParams().baud <= 0)
    throw std::invalid_argument("meter.baud must be positive");

  if (cfg.getSerialParams().dataBits < 5 || cfg.getSerialParams().dataBits > 8)
    throw std::invalid_argument("meter.data_bits must be between 5 and 8");

  if (!(cfg.getSerialParams().stopBits == 1 ||
        cfg.getSerialParams().stopBits == 2))
    throw std::invalid_argument("meter.stop_bits must be 1 or 2");

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

// Implementation of MeterConfig helper methods

MeterConfig::SerialParams MeterConfig::getSerialParams() const {
  // Start with fallback defaults
  SerialParams params{9600, 7, 1, SerialParity::Even};

  // Apply preset if specified
  if (preset.has_value()) {
    applyPresetDefaults(preset.value(), params.baud, params.dataBits,
                        params.stopBits, params.parity);
  }

  // Apply manual overrides (these take precedence)
  if (baud.has_value())
    params.baud = baud.value();
  if (dataBits.has_value())
    params.dataBits = dataBits.value();
  if (stopBits.has_value())
    params.stopBits = stopBits.value();
  if (parity.has_value())
    params.parity = parity.value();

  return params;
}