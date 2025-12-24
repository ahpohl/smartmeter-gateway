#include "mqtt_client.h"
#include "config_yaml.h"
#include "signal_handler.h"
#include <functional>
#include <mosquitto.h>
#include <spdlog/spdlog.h>

MqttClient::MqttClient(const MqttConfig &cfg, SignalHandler &signalHandler)
    : cfg_(cfg), handler_(signalHandler) {

  // Setup mqtt logger
  mqttLogger_ = spdlog::get("mqtt");
  if (!mqttLogger_)
    mqttLogger_ = spdlog::default_logger();

  // Create Mosquitto client
  mosquitto_lib_init();
  mosq_ = mosquitto_new(nullptr, true, this);
  if (!mosq_) {
    mqttLogger_->critical("Failed to create mosquitto client");
    handler_.shutdown();
  }

  // Set username/password if provided
  if (cfg_.user.has_value()) {
    mosquitto_username_pw_set(mosq_, opt_c_str(cfg_.user),
                              opt_c_str(cfg_.password));
  }

  // Configure automatic reconnect delay
  mosquitto_reconnect_delay_set(mosq_, cfg_.reconnectDelay->min,
                                cfg_.reconnectDelay->max,
                                cfg_.reconnectDelay->exponential);

  // Set Mosquitto callbacks
  mosquitto_connect_callback_set(mosq_, MqttClient::onConnect);
  mosquitto_disconnect_callback_set(mosq_, MqttClient::onDisconnect);
  mosquitto_log_callback_set(mosq_, MqttClient::onLog);

  // Start Mosquitto network loop
  int rc = mosquitto_loop_start(mosq_);
  if (rc != MOSQ_ERR_SUCCESS) {
    mqttLogger_->critical("Failed to start mosquitto network loop: {} ({})",
                          mosquitto_strerror(rc), rc);
    handler_.shutdown();
  }

  // Connect to broker
  rc = mosquitto_connect_async(mosq_, opt_c_str(cfg_.broker), cfg_.port, 60);
  if (rc != MOSQ_ERR_SUCCESS) {
    mqttLogger_->warn("MQTT: initial connect failed (async): {}",
                      mosquitto_strerror(rc));
  }

  // Start worker thread to process queue
  worker_ = std::thread(&MqttClient::run, this);
}

MqttClient::~MqttClient() {
  cv_.notify_all();
  if (worker_.joinable())
    worker_.join();

  if (mosq_) {
    mosquitto_disconnect(mosq_);

    // Give the disconnect callback time to execute and log
    // mosquitto_disconnect() is async, the callback runs on another thread
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    mosquitto_loop_stop(mosq_, true);
    mosquitto_destroy(mosq_);
    mosq_ = nullptr;
  }

  mosquitto_lib_cleanup();
}

bool MqttClient::hasQueuedMessages() const {
  return std::any_of(topicQueues_.begin(), topicQueues_.end(),
                     [](const auto &p) { return !p.second.empty(); });
}

void MqttClient::publish(const std::string &payload, const std::string &topic) {
  std::unique_lock<std::mutex> lock(mutex_);

  // Duplicate suppression per topic
  std::size_t payloadHash = std::hash<std::string>{}(payload);
  auto itHash = lastPayloadHashes_.find(topic);
  if (itHash != lastPayloadHashes_.end() && itHash->second == payloadHash)
    return;
  lastPayloadHashes_[topic] = payloadHash;

  // Per-topic queue reference
  auto &q = topicQueues_[topic];

  // If topic queue is full, drop oldest for that topic
  if (q.size() >= cfg_.queueSize) { // can be per-topic limit if you add one
    q.pop();                        // remove oldest for this topic
    droppedCount_[topic]++;         // track total drops for this topic
  }

  q.push({payload}); // push new message

  // Logging only if disconnected
  if (!connected_.load()) {
    if (droppedCount_[topic] > 0) {
      mqttLogger_->warn("MQTT queue full for topic '{}', dropped oldest "
                        "message (total dropped: {})",
                        topic, droppedCount_[topic]);
    } else {
      if (q.size() > 0) {
        mqttLogger_->debug(
            "Waiting for MQTT connection... ({} messages cached for '{}')",
            q.size(), topic);
      }
    }
  }

  cv_.notify_one();
}

void MqttClient::run() {
  while (handler_.isRunning()) {
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [&] {
      return (connected_.load() && hasQueuedMessages()) ||
             !handler_.isRunning();
    });

    if (!handler_.isRunning()) {
      if (!connected_.load()) {
        break;
      }
      if (hasQueuedMessages()) {
        mqttLogger_->debug("Shutdown detected, flushing remaining messages");
      }
    }

    for (auto &[topic, q] : topicQueues_) {
      while (!q.empty() && connected_.load()) {
        auto payload = q.front().payload;
        lock.unlock();

        int rc = mosquitto_publish(mosq_, nullptr, opt_c_str(topic),
                                   payload.size(), payload.c_str(), 1, true);

        lock.lock();
        if (rc == MOSQ_ERR_SUCCESS) {
          q.pop();
          mqttLogger_->debug("Published MQTT message to topic '{}': {}", topic,
                             payload);
          droppedCount_[topic] = 0;
        } else {
          mqttLogger_->error("MQTT publish failed for '{}': {}", topic,
                             mosquitto_strerror(rc));
          break;
        }
      }
    }
  }

  mqttLogger_->debug("MQTT run loop stopped.");
}

void MqttClient::onConnect(struct mosquitto *, void *obj, int rc) {
  MqttClient *self = static_cast<MqttClient *>(obj);

  self->connected_ = (rc == 0);
  self->cv_.notify_one();

  if (rc == 0)
    self->mqttLogger_->info("MQTT connected");
  else
    self->mqttLogger_->warn("MQTT connection failed: {} ({}), will retry...",
                            mosquitto_strerror(rc), rc);
}

void MqttClient::onDisconnect(struct mosquitto *mosq, void *obj, int rc) {
  MqttClient *self = static_cast<MqttClient *>(obj);

  self->connected_ = false;

  if (rc == 0) {
    self->mqttLogger_->info("MQTT disconnected");
  } else {
    self->mqttLogger_->warn(
        "MQTT disconnected unexpectedly: {} ({}), will retry...",
        mosquitto_strerror(rc), rc);
  }
}

void MqttClient::onLog(struct mosquitto *mosq, void *obj, int level,
                       const char *str) {
  MqttClient *self = static_cast<MqttClient *>(obj);

  // Map mosquitto log levels to spdlog
  spdlog::level::level_enum spdLevel = spdlog::level::info;
  switch (level) {
  case MOSQ_LOG_DEBUG:
    spdLevel = spdlog::level::trace;
    break;
  case MOSQ_LOG_INFO:
  case MOSQ_LOG_NOTICE:
    spdLevel = spdlog::level::info;
    break;
  case MOSQ_LOG_WARNING:
    spdLevel = spdlog::level::warn;
    break;
  case MOSQ_LOG_ERR:
    spdLevel = spdlog::level::err;
    break;
  default:
    spdLevel = spdlog::level::info;
    break;
  }

  self->mqttLogger_->log(spdLevel, "mosquitto [{}]: {}", level, str);
}
