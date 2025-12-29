#include "modbus_slave.h"
#include "common_registers.h"
#include "meter_registers.h"
#include "modbus_error.h"
#include "modbus_utils.h"
#include "signal_handler.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <expected>
#include <memory>
#include <modbus/modbus.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

ModbusSlave::ModbusSlave(const ModbusRootConfig &cfg,
                         SignalHandler &signalHandler)
    : cfg_(cfg), handler_(signalHandler) {

  modbusLogger_ = spdlog::get("modbus");
  if (!modbusLogger_)
    modbusLogger_ = spdlog::default_logger();

  auto listenAction = handleResult(startListener());

  // Start libmodbus connection thread
  if (cfg_.tcp)
    worker_ = std::thread(&ModbusSlave::tcpClientHandler, this);
  else
    worker_ = std::thread(&ModbusSlave::rtuClientHandler, this);
}

ModbusSlave::~ModbusSlave() {
  if (worker_.joinable())
    worker_.join();

  if (serverSocket_ != -1) {
    close(serverSocket_);
    modbusLogger_->info("Stopped Modbus {} listener",
                        (cfg_.tcp ? "TCP" : "RTU"));
  }
  if (listenCtx_)
    modbus_free(listenCtx_);
}

std::expected<void, ModbusError> ModbusSlave::startListener(void) {

  // --- Create register mapping ---
  if (!regs_.load()) {
    regs_.store(std::shared_ptr<modbus_mapping_t>(
        modbus_mapping_new(0, 0, MODBUS_REGISTERS, 0), ModbusDeleter{}));

    if (!regs_.load()) {
      return std::unexpected(
          ModbusError::fromErrno("Unable to allocate new Modbus mapping"));
    }
  }

  // Fill mapping with static SunSpec meter model
  auto regs = regs_.load();
  handleResult(
      ModbusUtils::packToModbus<uint32_t>(regs.get(), C001::SID, 0x53756e53));
  regs->tab_registers[C001::ID.ADDR] = 1;
  regs->tab_registers[C001::L.ADDR] = C001::SIZE;
  regs->tab_registers[C001::DA.ADDR] = cfg_.slaveId;

  if (cfg_.useFloatModel) {
    regs->tab_registers[M21X::ID.ADDR] = 213;
    regs->tab_registers[M21X::L.ADDR] = M21X::SIZE;
    regs->tab_registers[M_END::ID.withOffset(M_END::FLOAT_OFFSET).ADDR] =
        0xFFFF;
  } else {
    regs->tab_registers[M20X::ID.ADDR] = 203;
    regs->tab_registers[M20X::L.ADDR] = M20X::SIZE;
    regs->tab_registers[M_END::ID.ADDR] = 0xFFFF;
  }

  // Create new context based on config
  if (cfg_.tcp) {
    listenCtx_ = modbus_new_tcp_pi(opt_c_str(cfg_.tcp->listen),
                                   opt_c_str(std::to_string(cfg_.tcp->port)));
  } else {
    listenCtx_ =
        modbus_new_rtu(opt_c_str(cfg_.rtu->device), cfg_.rtu->baud, 'N', 8, 1);
  }
  if (!listenCtx_) {
    return std::unexpected(
        ModbusError::custom(ENOMEM, "Unable to create the libmodbus {} context",
                            (cfg_.tcp ? "TCP" : "RTU")));
  }

  // Attempt to start listener
  std::string mode = cfg_.tcp ? "TCP" : "RTU";
  std::string endpoint =
      cfg_.tcp ? cfg_.tcp->listen + ":" + std::to_string(cfg_.tcp->port)
               : cfg_.rtu->device;

  int rc;
  if (cfg_.tcp) {
    rc = modbus_tcp_pi_listen(listenCtx_, 16);
    if (rc != -1) {
      serverSocket_ = rc; // Store the socket on success
    }
  } else {
    rc = modbus_connect(listenCtx_);
  }

  if (rc == -1) {
    modbus_free(listenCtx_);
    listenCtx_ = nullptr;
    return std::unexpected(ModbusError::fromErrno(
        "Failed to start Modbus {} listener on '{}'", mode, endpoint));
  }

  modbusLogger_->info("Started Modbus {} listener on '{}'", mode, endpoint);

  return {};
}

MeterTypes::ErrorAction
ModbusSlave::handleResult(std::expected<void, ModbusError> &&result) {
  if (result) {
    return MeterTypes::ErrorAction::NONE;
  }

  const ModbusError &err = result.error();

  if (err.severity == ModbusError::Severity::FATAL) {
    // Fatal error occurred - initiate shutdown sequence
    modbusLogger_->error("FATAL Modbus error: {}", err.describe());
    handler_.shutdown();
    return MeterTypes::ErrorAction::SHUTDOWN;

  } else if (err.severity == ModbusError::Severity::TRANSIENT) {
    // Temporary error - disconnect and reconnect
    modbusLogger_->warn("Transient Modbus error: {}", err.describe());
    return MeterTypes::ErrorAction::RECONNECT;

  } else if (err.severity == ModbusError::Severity::SHUTDOWN) {
    // Shutdown already in progress - just exit cleanly
    modbusLogger_->trace("Modbus operation cancelled due to shutdown: {}",
                         err.describe());
    return MeterTypes::ErrorAction::SHUTDOWN;
  }

  return MeterTypes::ErrorAction::NONE;
}

void ModbusSlave::updateValues(MeterTypes::Values values) {
  if (!handler_.isRunning()) {
    modbusLogger_->error("updateValues(): Shutdown in progress");
    return;
  }

  // Snapshot current mapping
  auto oldRegs = regs_.load();
  if (!oldRegs) {
    modbusLogger_->error("updateValues(): No existing mapping to base on");
    handler_.shutdown();
    return;
  }

  auto newRegs = std::shared_ptr<modbus_mapping_t>(
      modbus_mapping_new(0, 0, MODBUS_REGISTERS, 0), ModbusDeleter{});
  if (!newRegs) {
    modbusLogger_->error(
        "updateValues(): Unable to allocate new Modbus mapping");
    handler_.shutdown();
    return;
  }

  // Copy entire register block from old to new (fast)
  std::memcpy(newRegs->tab_registers, oldRegs->tab_registers,
              static_cast<size_t>(newRegs->nb_registers) * sizeof(uint16_t));

  if (cfg_.useFloatModel) {
    handleResult(ModbusUtils::packToModbus<float>(newRegs.get(), M21X::PHVPHA,
                                                  values.phase1.voltage));
    handleResult(ModbusUtils::packToModbus<float>(newRegs.get(), M21X::PHVPHB,
                                                  values.phase2.voltage));
    handleResult(ModbusUtils::packToModbus<float>(newRegs.get(), M21X::PHVPHC,
                                                  values.phase3.voltage));
    handleResult(
        ModbusUtils::packToModbus<float>(newRegs.get(), M21X::W, values.power));
    handleResult(ModbusUtils::packToModbus<float>(newRegs.get(), M21X::WPHA,
                                                  values.phase1.power));
    handleResult(ModbusUtils::packToModbus<float>(newRegs.get(), M21X::WPHB,
                                                  values.phase1.power));
    handleResult(ModbusUtils::packToModbus<float>(newRegs.get(), M21X::WPHC,
                                                  values.phase1.power));
    handleResult(ModbusUtils::packToModbus<float>(
        newRegs.get(), M21X::TOTWH_IMP, values.energy));
  } else {
    handleResult(ModbusUtils::floatToModbus(
        newRegs.get(), M20X::PHVPHA, M20X::PPV, values.phase1.voltage, 2));
    handleResult(ModbusUtils::floatToModbus(
        newRegs.get(), M20X::PHVPHB, M20X::PPV, values.phase2.voltage, 2));
    handleResult(ModbusUtils::floatToModbus(
        newRegs.get(), M20X::PHVPHC, M20X::PPV, values.phase3.voltage, 2));
    handleResult(ModbusUtils::floatToModbus(newRegs.get(), M20X::W, M20X::W_SF,
                                            values.power, 0));
    handleResult(ModbusUtils::floatToModbus(
        newRegs.get(), M20X::WPHA, M20X::W_SF, values.phase1.power, 0));
    handleResult(ModbusUtils::floatToModbus(
        newRegs.get(), M20X::WPHB, M20X::W_SF, values.phase2.power, 0));
    handleResult(ModbusUtils::floatToModbus(
        newRegs.get(), M20X::WPHC, M20X::W_SF, values.phase3.power, 0));
    handleResult(ModbusUtils::floatToModbus(newRegs.get(), M20X::TOTWH_IMP,
                                            M20X::TOTWH_SF, values.energy * 1e3,
                                            1));
  }

  regs_.store(newRegs);
}

void ModbusSlave::updateDevice(MeterTypes::Device device) {
  if (!handler_.isRunning()) {
    modbusLogger_->error("updateDevice(): Shutdown in progress");
    return;
  }

  if (deviceUpdated_)
    return;

  // Snapshot current mapping
  auto oldRegs = regs_.load();
  if (!oldRegs) {
    modbusLogger_->error("updateDevice(): No existing mapping to base on");
    handler_.shutdown();
    return;
  }

  auto newRegs = std::shared_ptr<modbus_mapping_t>(
      modbus_mapping_new(0, 0, MODBUS_REGISTERS, 0), ModbusDeleter{});
  if (!newRegs) {
    modbusLogger_->error(
        "updateDevice(): Unable to allocate new Modbus mapping");
    handler_.shutdown();
    return;
  }

  // Copy entire register block from old to new (fast)
  std::memcpy(newRegs->tab_registers, oldRegs->tab_registers,
              static_cast<size_t>(MODBUS_REGISTERS) * sizeof(uint16_t));

  handleResult(ModbusUtils::packToModbus<std::string>(newRegs.get(), C001::MN,
                                                      device.manufacturer));
  handleResult(ModbusUtils::packToModbus<std::string>(newRegs.get(), C001::MD,
                                                      device.model));
  handleResult(ModbusUtils::packToModbus<std::string>(newRegs.get(), C001::VR,
                                                      device.fwVersion));
  handleResult(ModbusUtils::packToModbus<std::string>(newRegs.get(), C001::SN,
                                                      device.serialNumber));

  regs_.store(newRegs);
  deviceUpdated_ = true;
}

void ModbusSlave::tcpClientWorker(int socket) {

  modbus_t *ctx = modbus_new_tcp(nullptr, 0);
  if (!ctx) {
    modbusLogger_->error("tcpClientWorker(): Unable to create client context");
    close(socket);
    return;
  }
  modbus_set_socket(ctx, socket);

  // Set slave/unit ID
  if (modbus_set_slave(ctx, cfg_.slaveId) == -1) {
    modbusLogger_->error("tcpClientWorker(): Setting slave id '{}' failed",
                         cfg_.slaveId);
    modbus_close(ctx);
    modbus_free(ctx);
    close(socket);
    return;
  }

  // Set request timeout - controls how long modbus_receive() waits per call
  modbus_set_indication_timeout(ctx, cfg_.requestTimeout, 0);

  // Set libmodbus debug - enable only if logger is at trace level
  if (modbusLogger_->level() == spdlog::level::trace) {
    if (modbus_set_debug(ctx, true) == -1) {
      modbusLogger_->warn("tcpClientWorker(): Unable to set debug flag");
    }
  }

  modbusLogger_->debug("tcpClientWorker(): client connected (slave_id={}, "
                       "request_timeout={}s, idle_timeout={}s)",
                       cfg_.slaveId, cfg_.requestTimeout, cfg_.idleTimeout);

  uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];

  // Track last activity time for idle timeout
  auto lastActivity = std::chrono::steady_clock::now();
  auto idleTimeout = std::chrono::seconds(cfg_.idleTimeout);

  while (handler_.isRunning()) {
    int rc = modbus_receive(ctx, query);

    if (rc > 0) {
      // Valid request received - update activity timestamp
      lastActivity = std::chrono::steady_clock::now();

      auto regs = regs_.load();
      if (!regs) {
        modbusLogger_->error("tcpClientWorker(): no Modbus mapping available");
        break;
      }
      if (modbus_reply(ctx, query, rc, regs.get()) == -1) {
        modbusLogger_->warn("tcpClientWorker(): Modbus reply failed: {}",
                            modbus_strerror(errno));
        break;
      }
    } else if (rc == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
        // Request timeout - check if we should continue running
        if (!handler_.isRunning())
          break;

        // Check idle timeout
        auto now = std::chrono::steady_clock::now();
        if (now - lastActivity > idleTimeout) {
          modbusLogger_->info(
              "tcpClientWorker(): client idle timeout ({}s), disconnecting",
              cfg_.idleTimeout);
          break;
        }

        continue;
      }
      if (errno == EINTR) {
        // Interrupted by signal - check if we should continue
        if (!handler_.isRunning())
          break;
        continue;
      }
      // Connection closed or error
      modbusLogger_->debug("tcpClientWorker(): client disconnected: {}",
                           modbus_strerror(errno));
      break;
    } else if (rc == 0) {
      // Connection closed by client
      modbusLogger_->debug("tcpClientWorker(): client closed connection");
      break;
    }
  }

  modbus_close(ctx);
  modbus_free(ctx);
  close(socket);
}

void ModbusSlave::rtuClientHandler() {

  // Set slave/unit ID
  if (modbus_set_slave(listenCtx_, cfg_.slaveId) == -1) {
    modbusLogger_->error("rtuClientHandler() Setting slave id '{}' failed",
                         cfg_.slaveId);
    modbus_close(listenCtx_);
    modbus_free(listenCtx_);
    return;
  }

  // Set request timeout - controls how long modbus_receive() waits per call
  modbus_set_indication_timeout(listenCtx_, cfg_.requestTimeout, 0);

  // Set libmodbus debug - enable only if logger is at trace level
  if (modbusLogger_->level() == spdlog::level::trace) {
    if (modbus_set_debug(listenCtx_, true) == -1) {
      modbusLogger_->warn("rtuClientHandler() Unable to set debug flag");
    }
  }

  modbusLogger_->debug("rtuClientHandler(): listening for requests "
                       "(slave_id={}, request_timeout={}s, idle_timeout={}s)",
                       cfg_.slaveId, cfg_.requestTimeout, cfg_.idleTimeout);

  uint8_t query[MODBUS_RTU_MAX_ADU_LENGTH];

  // Track last activity time for idle timeout
  auto lastActivity = std::chrono::steady_clock::now();
  auto idleTimeout = std::chrono::seconds(cfg_.idleTimeout);

  while (handler_.isRunning()) {
    int rc = modbus_receive(listenCtx_, query);

    // --- Valid request received ---
    if (rc > 0) {
      lastActivity = std::chrono::steady_clock::now();

      auto regs = regs_.load();
      if (!regs) {
        modbusLogger_->error("rtuClientHandler(): no Modbus mapping available");
        handler_.shutdown();
        break;
      }

      if (modbus_reply(listenCtx_, query, rc, regs.get()) == -1) {
        modbusLogger_->warn("rtuClientHandler(): reply failed: {}",
                            modbus_strerror(errno));
      }
      continue;
    }

    // --- Ignored frame (wrong slave ID, CRC error filtered by libmodbus) ---
    if (rc == 0) {
      continue;
    }

    // --- Error handling (rc == -1) ---

    // Timeout - expected, check idle and continue
    if (errno == ETIMEDOUT || errno == EAGAIN || errno == EWOULDBLOCK) {
      auto now = std::chrono::steady_clock::now();
      if (now - lastActivity > idleTimeout) {
        modbusLogger_->debug("rtuClientHandler(): idle for {}s",
                             cfg_.idleTimeout);
        lastActivity = now;
      }
      continue;
    }

    // Interrupted by signal - check shutdown flag
    if (errno == EINTR) {
      continue; // isRunning() checked at loop start
    }

    // Fatal serial errors - cannot recover
    if (errno == EBADF || errno == EIO) {
      modbusLogger_->error("rtuClientHandler(): fatal serial error:  {}",
                           modbus_strerror(errno));
      handler_.shutdown();
      break;
    }

    // Other errors - log and try to continue
    modbusLogger_->warn("rtuClientHandler(): receive error: {}",
                        modbus_strerror(errno));
  }

  modbusLogger_->debug("Modbus RTU slave run loop stopped");
}

void ModbusSlave::tcpClientHandler(void) {

  // TCP mode - accept connections and spawn client threads
  if (serverSocket_ == -1) {
    modbusLogger_->error(
        "tcpClientHandler(): server socket is invalid, cannot start");
    handler_.shutdown();
    return;
  }

  struct pollfd pfd;
  pfd.fd = serverSocket_;
  pfd.events = POLLIN;

  while (handler_.isRunning()) {

    // Use timeout to allow periodic checking of isRunning()
    int ret = poll(&pfd, 1, 500);

    if (ret < 0) {
      if (errno == EINTR) {
        // Interrupted by signal - check if we should continue
        continue;
      }
      modbusLogger_->error("tcpClientHandler(): poll failed: {}",
                           strerror(errno));
      handler_.shutdown();
      break;
    } else if (ret == 0) {
      // Timeout - loop back and check isRunning()
      continue;
    }

    // Check for incoming connection
    if (pfd.revents & POLLIN) {
      struct sockaddr_storage addr;
      socklen_t addrlen = sizeof(addr);
      int clientSocket =
          accept(serverSocket_, (struct sockaddr *)&addr, &addrlen);

      if (clientSocket < 0) {
        if (errno == EINTR) {
          // Interrupted - check if we should continue
          if (!handler_.isRunning())
            break;
          continue;
        }
        modbusLogger_->warn("tcpClientHandler(): accept failed: {}",
                            strerror(errno));
        continue;
      }

      // spawn a thread to handle the client
      {
        std::lock_guard<std::mutex> lock(clientMutex_);
        clientThreads_.emplace_back(
            [clientSocket, this]() { tcpClientWorker(clientSocket); });
      }
    }

    // Check for socket errors
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      modbusLogger_->error("tcpClientHandler(): server socket error");
      handler_.shutdown();
      break;
    }
  }

  // Shutdown: close server socket to unblock any pending accepts
  if (serverSocket_ != -1) {
    shutdown(serverSocket_, SHUT_RDWR);
  }

  // Join all client threads
  {
    std::lock_guard<std::mutex> lock(clientMutex_);
    for (auto &t : clientThreads_) {
      if (t.joinable())
        t.join();
    }
    clientThreads_.clear();
  }

  modbusLogger_->debug("Modbus TCP slave run loop stopped");
}