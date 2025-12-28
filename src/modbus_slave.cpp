#include "modbus_slave.h"
#include "common_registers.h"
#include "meter_registers.h"
#include "modbus_error.h"
#include "modbus_utils.h"
#include "signal_handler.h"
#include <atomic>
#include <csignal>
#include <expected>
#include <memory>
#include <modbus/modbus.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

ModbusSlave::ModbusSlave(const ModbusRootConfig &cfg,
                         SignalHandler &signalHandler)
    : cfg_(cfg), handler_(signalHandler) {

  modbusLogger_ = spdlog::get("modbus");
  if (!modbusLogger_)
    modbusLogger_ = spdlog::default_logger();

  auto listenAction = handleResult(startListener());

  // Start libmodbus loop thread
  worker_ = std::thread(&ModbusSlave::runLoop, this);
}

ModbusSlave::~ModbusSlave() {
  if (worker_.joinable())
    worker_.join();

  if (serverSocket_ != -1) {
    close(serverSocket_);
    modbusLogger_->info("Stopped Modbus {} listener",
                        (cfg_.tcp ? "TCP" : "RTU"));
  }
  if (ctx_)
    modbus_free(ctx_);
}

std::expected<void, ModbusError> ModbusSlave::startListener(void) {

  // setup signal fd
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);

  // Block the signals so they are delivered to signalfd (and not to default
  // handlers)
  if (pthread_sigmask(SIG_BLOCK, &mask, nullptr) != 0) {
    return std::unexpected(
        ModbusError::custom(EINVAL, "startListener: pthread_sigmask"));
  }

  sfd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sfd_ < 0) {
    return std::unexpected(
        ModbusError::custom(EINVAL, "startListener(): signalfd"));
  }

  fds_[0].fd = serverSocket_;
  fds_[0].events = POLLIN;
  fds_[1].fd = sfd_;
  fds_[1].events = POLLIN;

  // --- Create register mapping ---
  if (!regs_.load()) {
    regs_.store(std::shared_ptr<modbus_mapping_t>(
        modbus_mapping_new(0, 0, 65535, 0), ModbusDeleter{}));

    if (!regs_.load()) {
      return std::unexpected(ModbusError::fromErrno(
          "startListener(): Unable to allocate new Modbus mapping"));
    }
  }

  // Fill mapping with static SunSpec meter model
  auto regs = regs_.load();
  ModbusUtils::packToModbus<uint32_t>(&regs->tab_registers[C001::SID.ADDR],
                                      0x53756e53);
  regs->tab_registers[C001::ID.ADDR] = 1;
  regs->tab_registers[C001::L.ADDR] = C001::SIZE;
  regs->tab_registers[C001::DA.ADDR] = cfg_.slaveId;

  if (cfg_.useFloatModel) {
    regs->tab_registers[M21X::ID.ADDR] = 213;
    regs->tab_registers[M21X::L.ADDR] = M21X::SIZE;
    regs->tab_registers[M_END::ID.withOffset(M_END::FLOAT_OFFSET).ADDR] =
        0xFFFF;
  } else {
    regs->tab_registers[M20X::ID.ADDR] = 201;
    regs->tab_registers[M20X::L.ADDR] = M20X::SIZE;
    regs->tab_registers[M_END::ID.ADDR] = 0xFFFF;
  }

  // Create new context based on config
  if (cfg_.tcp) {
    ctx_ = modbus_new_tcp_pi(opt_c_str(cfg_.tcp->listen),
                             opt_c_str(std::to_string(cfg_.tcp->port)));
  } else {
    ctx_ =
        modbus_new_rtu(opt_c_str(cfg_.rtu->device), cfg_.rtu->baud, 'N', 8, 1);
  }
  if (!ctx_) {
    return std::unexpected(ModbusError::custom(
        ENOMEM, "startListener(): Unable to create the libmodbus {} context",
        (cfg_.tcp ? "TCP" : "RTU")));
  }

  // Set slave/unit ID
  if (modbus_set_slave(ctx_, cfg_.slaveId) == -1) {
    modbus_free(ctx_);
    ctx_ = nullptr;
    return std::unexpected(ModbusError::fromErrno(
        "startListener(): Setting slave id '{}' failed", cfg_.slaveId));
  }

  // set indication timeout
  modbus_set_indication_timeout(ctx_, cfg_.indicationTimeout->sec,
                                cfg_.indicationTimeout->usec);

  // Set libmodbus debug
  // Enable debug only if logger is at trace level
  if (modbusLogger_->level() == spdlog::level::trace) {
    if (modbus_set_debug(ctx_, true) == -1) {
      modbus_free(ctx_);
      ctx_ = nullptr;
      return std::unexpected(ModbusError::fromErrno(
          "startListener(): Unable to set the libmodbus debug flag"));
    }
  }

  // Attempt to start listener
  std::string mode = cfg_.tcp ? "TCP" : "RTU";
  std::string endpoint =
      cfg_.tcp ? cfg_.tcp->listen + ":" + std::to_string(cfg_.tcp->port)
               : cfg_.rtu->device;

  int rc;
  if (cfg_.tcp) {
    rc = modbus_tcp_pi_listen(ctx_, 16);
    if (rc != -1) {
      serverSocket_ = rc; // Store the socket on success
    }
  } else {
    rc = modbus_connect(ctx_);
  }

  if (rc == -1) {
    modbus_free(ctx_);
    ctx_ = nullptr;
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
      modbus_mapping_new(0, 0, 65535, 0), ModbusDeleter{});
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
    modbus_set_float_abcd(values.phase1.voltage,
                          &newRegs->tab_registers[M21X::PHVPHA.ADDR]);
    modbus_set_float_abcd(values.phase2.voltage,
                          &newRegs->tab_registers[M21X::PHVPHB.ADDR]);
    modbus_set_float_abcd(values.phase3.voltage,
                          &newRegs->tab_registers[M21X::PHVPHC.ADDR]);
    modbus_set_float_abcd(values.power, &newRegs->tab_registers[M21X::W.ADDR]);
    modbus_set_float_abcd(values.phase1.power,
                          &newRegs->tab_registers[M21X::WPHA.ADDR]);
    modbus_set_float_abcd(values.phase2.power,
                          &newRegs->tab_registers[M21X::WPHB.ADDR]);
    modbus_set_float_abcd(values.phase3.power,
                          &newRegs->tab_registers[M21X::WPHC.ADDR]);
    modbus_set_float_abcd(values.energy,
                          &newRegs->tab_registers[M21X::TOTWH_IMP.ADDR]);
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
                                            M20X::TOTWH_SF, values.energy, 1));
  }

  regs_.store(newRegs);
}

void ModbusSlave::updateDevice(MeterTypes::Device device) {
  if (!handler_.isRunning()) {
    modbusLogger_->error("updateDevice(): Shutdown in progress");
    return;
  }

  // Snapshot current mapping
  auto oldRegs = regs_.load();
  if (!oldRegs) {
    modbusLogger_->error("updateDevice(): No existing mapping to base on");
    handler_.shutdown();
    return;
  }

  auto newRegs = std::shared_ptr<modbus_mapping_t>(
      modbus_mapping_new(0, 0, 65535, 0), ModbusDeleter{});
  if (!newRegs) {
    modbusLogger_->error(
        "updateDevice(): Unable to allocate new Modbus mapping");
    handler_.shutdown();
    return;
  }

  // Copy entire register block from old to new (fast)
  std::memcpy(newRegs->tab_registers, oldRegs->tab_registers,
              static_cast<size_t>(65535) * sizeof(uint16_t));

  handleResult(ModbusUtils::stringToModbus(
      &newRegs->tab_registers[C001::MN.ADDR], device.manufacturer));
  handleResult(ModbusUtils::stringToModbus(
      &newRegs->tab_registers[C001::MD.ADDR], device.model));
  handleResult(ModbusUtils::stringToModbus(
      &newRegs->tab_registers[C001::VR.ADDR], device.fwVersion));
  handleResult(ModbusUtils::stringToModbus(
      &newRegs->tab_registers[C001::SN.ADDR], device.serialNumber));

  regs_.store(newRegs);
}

void ModbusSlave::clientWorker(int clientSocket) {

  modbus_t *ctx = modbus_new_tcp(nullptr, 0);
  if (!ctx) {
    modbusLogger_->error("clientWorker(): Unable to create client context");
    handler_.shutdown();
    return;
  }
  modbus_set_socket(ctx, clientSocket);
  modbus_set_slave(ctx, 1);

  struct timeval tv{};
  tv.tv_sec = 0;
  tv.tv_usec = 300;
  if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    modbusLogger_->error("clientWorker(): Unable to set sockopt SO_RCVTIMEO");
    modbus_close(ctx);
    modbus_free(ctx);
    handler_.shutdown();
    return;
  }

  clientSocket = modbus_tcp_pi_accept(ctx_, &serverSocket_);

  uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];

  while (handler_.isRunning()) {
    int rc = modbus_receive(ctx_, query);
    if (rc > 0) {
      auto regs = regs_.load();
      if (!regs) {
        modbusLogger_->error("clientWorker(): no Modbus mapping available");
        break;
      }
      if (modbus_reply(ctx_, query, rc, regs.get()) == -1) {
        modbusLogger_->warn("clientWorker(): Modbus reply failed");
        break;
      }
    } else if (rc == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) {
        modbusLogger_->warn("clientWorker(): timeout");
        if (!handler_.isRunning())
          break;
        continue;
      }
      if (errno == EINTR) {
        modbusLogger_->warn("clientWorker(): interrupted by signal");
        if (!handler_.isRunning())
          break;
        continue;
      }
      modbusLogger_->warn("clientWorker(): client disconnected");
      break;
    }
  }

  modbus_close(ctx);
  modbus_free(ctx);
  close(clientSocket);
}

void ModbusSlave::runLoop() {

  while (handler_.isRunning()) {

    int ret = poll(fds_, 2, -1);
    if (ret < 0) {
      if (errno == EINTR)
        continue; // interrupted, re-poll
      modbusLogger_->error("poll");
      handler_.shutdown();
      break;
    }

    // Check for signal
    if (fds_[1].revents & POLLIN) {
      struct signalfd_siginfo fdsi;
      ssize_t s = read(sfd_, &fdsi, sizeof(fdsi));
      if (s != sizeof(fdsi)) {
        continue;
      }
      if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
        modbusLogger_->debug("Signal received, shutting down...");
        handler_.shutdown();
        close(serverSocket_);
        break;
      }
    }

    // Check for incoming connection
    if (fds_[0].revents & POLLIN) {
      struct sockaddr_storage addr;
      socklen_t addrlen = sizeof(addr);
      int clientSocket =
          accept(serverSocket_, (struct sockaddr *)&addr, &addrlen);
      if (clientSocket < 0) {
        if (errno == EINTR) {
          continue;
        }
        modbusLogger_->debug("accepted new client");
        continue;
      }

      // spawn a thread to handle the client
      {
        std::lock_guard<std::mutex> lock(mbMutex_);
        clientThreads_.emplace_back(
            [clientSocket, this]() { clientWorker(clientSocket); });
      }
    }
  }
  // Shutdown: join all client threads
  {
    std::lock_guard<std::mutex> lock(mbMutex_);
    for (auto &t : clientThreads_) {
      if (t.joinable())
        t.join();
    }
    clientThreads_.clear();
  }

  // cleanup
  close(sfd_);

  modbusLogger_->debug("Modbus slave run loop stopped");
}