#include "config.h"
#include "config_yaml.h"
#include "logger.h"
// #include "modbus_slave.h"
#include "meter.h"
#include "mqtt_client.h"
#include "signal_handler.h"
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <iostream>
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

  // --- Setup signals and shutdown
  SignalHandler handler;

  // --- Start MQTT consumer ---
  MqttClient mqtt(cfg.mqtt, handler);

  // --- Start meter
  Meter meter(cfg.meter, handler);

  // --- Setup callbacks
  meter.setUpdateCallback([&mqtt, &cfg](const std::string &jsonDump) {
    mqtt.publish(jsonDump, cfg.mqtt.topic + "/values");
  });
  meter.setAvailabilityCallback([&mqtt, &cfg](const std::string &availability) {
    mqtt.publish(availability, cfg.mqtt.topic + "/availability");
  });

  // --- Wait for shutdown signal ---
  handler.wait();

  // --- Shutdown ---
  mainLogger->info("Shutting down due to signal {} ({})", handler.signalName(),
                   handler.signal());

  return EXIT_SUCCESS;
}