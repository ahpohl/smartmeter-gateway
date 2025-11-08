#ifndef SIGNAL_HANDLER_H_
#define SIGNAL_HANDLER_H_

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <mutex>

class SignalHandler {
public:
  explicit SignalHandler(void) : running_(true) {
    struct sigaction action{};
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = [](int sig, siginfo_t *, void *) {
      if (instance_ && instance_->running_.load()) {
        instance_->signal_ = sig;
        instance_->shutdown();
      }
    };
    sigemptyset(&action.sa_mask);
    instance_ = this;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
  }

  ~SignalHandler() {
    struct sigaction defaultAction{};
    defaultAction.sa_handler = SIG_DFL;
    sigaction(SIGINT, &defaultAction, nullptr);
    sigaction(SIGTERM, &defaultAction, nullptr);
    instance_ = nullptr;
  }

  // --- Delete copy and assignment ---
  SignalHandler(const SignalHandler &) = delete;
  SignalHandler &operator=(const SignalHandler &) = delete;

  // --- Programmatic shutdown ---
  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mtx_);
      running_.store(false);
    }
    cv_.notify_all();
  }

  // --- Wait for shutdown ---
  void wait() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [&] { return !running_.load(); });
  }

  const char *signalName() const {
    return signal_ ? strsignal(signal_) : "internal request";
  }

  int signal() const { return signal_; }
  bool isRunning() const { return running_.load(); }

private:
  std::atomic<bool> running_;
  std::mutex mtx_;
  std::condition_variable cv_;
  static inline SignalHandler *instance_ = nullptr;
  std::atomic<int> signal_{0};
};

#endif /* SIGNAL_HANDLER_H_ */
