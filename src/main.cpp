#include "config.h"
#include "config_yaml.h"
#include "logger.h"
#include "meter.h"
#include "meter_types.h"
#include "modbus_slave.h"
#include "mqtt_client.h"
#include "privileges.h"
#include "signal_handler.h"
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main(int argc, char *argv[]) {

  // --- Command line parsing ---
  CLI::App app{PROJECT_NAME " - " PROJECT_DESCRIPTION};

  // Version string
  std::string versionStr = std::string(PROJECT_NAME) + " v" + PROJECT_VERSION +
                           " (" + GIT_COMMIT_HASH + ")";

  app.set_version_flag("-V,--version", versionStr);

  std::string config;
  auto configOption = app.add_option("-c,--config", config, "Set config file")
                          ->required()
                          ->envname("METER_CONFIG")
                          ->check(CLI::ExistingFile);

  // Optional: prevent specifying both at the same time in help/UX
  configOption->excludes("--version");

  // Privilege dropping options
  std::string runUser;
  std::string runGroup;
  app.add_option("-u,--user", runUser,
                 "Drop privileges to this user after startup")
      ->envname("METER_USER");
  app.add_option("-g,--group", runGroup,
                 "Drop privileges to this group after startup")
      ->envname("METER_GROUP");

  CLI11_PARSE(app, argc, argv);

  // --- Load config ---
  Config cfg;
  try {
    cfg = loadConfig(config);
  } catch (const std::exception &ex) {
    std::cerr << "Error loading config: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }

  // --- Setup logging ---
  setupLogging(cfg.logger);
  std::shared_ptr<spdlog::logger> mainLogger = spdlog::get("main");
  if (!mainLogger)
    mainLogger = spdlog::default_logger();
  mainLogger->info("Starting {} with config '{}'", PROJECT_NAME, config);

  // Warn if --user/--group specified but not running as root
  if (!Privileges::isRoot() && !runUser.empty()) {
    mainLogger->error(
        "--user/--group options specified, but not running as root");
    mainLogger->error("Either run as root, or remove --user/--group options");
    return EXIT_FAILURE;
  }

  // Check if we need root for TCP privileged port
  if (!Privileges::isRoot() && cfg.modbus && cfg.modbus->tcp &&
      cfg.modbus->tcp->port < 1024) {
    mainLogger->error("Modbus TCP port {} requires root privileges, but not "
                      "running as root",
                      cfg.modbus->tcp->port);
    mainLogger->error("Either run as root with --user/--group options, or "
                      "change Modbus port to >= 1024");
    return EXIT_FAILURE;
  }

  // Warn if running as root without privilege drop
  if (Privileges::isRoot() && runUser.empty()) {
    mainLogger->warn("Running as root without privilege dropping - "
                     "consider using --user/--group options");
  }

  // --- Setup signals and shutdown
  SignalHandler handler;

  // --- Start Modbus consumer (optional) ---
  std::unique_ptr<ModbusSlave> slave;
  if (cfg.modbus) {
    slave = std::make_unique<ModbusSlave>(cfg.modbus.value(), handler);
  } else {
    mainLogger->info("Modbus slave disabled (no modbus section in config)");
  }

  // --- Drop privileges after binding to privileged ports ---
  if (!runUser.empty() && Privileges::isRoot()) {
    try {
      Privileges::drop(runUser, runGroup);
      mainLogger->info("Dropped privileges to user '{}' group '{}'",
                       Privileges::getCurrentUser(),
                       Privileges::getCurrentGroup());
    } catch (const std::exception &ex) {
      mainLogger->error("Failed to drop privileges: {}", ex.what());
      return EXIT_FAILURE;
    }
  }

  // --- Start MQTT consumer ---
  MqttClient mqtt(cfg.mqtt, handler);

  // --- Start meter producer
  Meter meter(cfg.meter, handler);

  // --- Setup callbacks
  meter.setUpdateCallback(
      [&cfg, &mqtt, &slave](std::string jsonDump, MeterTypes::Values values) {
        mqtt.publish(std::move(jsonDump), cfg.mqtt.topic + "/values");
        if (slave) {
          slave->updateValues(std::move(values));
        }
      });
  meter.setDeviceCallback(
      [&cfg, &mqtt, &slave](std::string jsonDump, MeterTypes::Device device) {
        mqtt.publish(std::move(jsonDump), cfg.mqtt.topic + "/device");
        if (slave) {
          slave->updateDevice(std::move(device));
        }
      });
  meter.setAvailabilityCallback([&mqtt, &cfg](std::string availability) {
    mqtt.publish(std::move(availability), cfg.mqtt.topic + "/availability");
  });

  // --- Wait for shutdown signal ---
  handler.wait();

  // --- Shutdown ---
  mainLogger->info("Shutting down due to signal {} ({})", handler.signalName(),
                   handler.signal());

  return EXIT_SUCCESS;
}