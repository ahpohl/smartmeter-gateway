#ifndef METER_ERROR_H_
#define METER_ERROR_H_

#include <cerrno>
#include <cstring>
#include <expected>
#include <format>
#include <string>

struct MeterError {
public:
  enum class Severity { TRANSIENT, FATAL, SHUTDOWN };

  int code;
  std::string message;
  Severity severity;

  static MeterError fromErrno(const std::string &msg) {
    return {errno, msg, deduceSeverity(errno)};
  }

  template <typename... Args>
  static MeterError fromErrno(std::format_string<Args...> fmt, Args &&...args) {
    return {errno, std::format(fmt, std::forward<Args>(args)...),
            deduceSeverity(errno)};
  }

  static MeterError custom(int c, const std::string &msg) {
    return {c, msg, deduceSeverity(c)};
  }

  template <typename... Args>
  static MeterError custom(int code, std::format_string<Args...> fmt,
                           Args &&...args) {
    return {code, std::format(fmt, std::forward<Args>(args)...),
            deduceSeverity(code)};
  }

  template <typename T> static T getOrThrow(std::expected<T, MeterError> res) {
    if (res)
      return *res;
    else
      throw res.error();
  }

  std::string describe() const {
    return std::format("{}: {} (code {})", message, strerror(code), code);
  }

private:
  static Severity deduceSeverity(int c) {
    switch (c) {
    case EINVAL:       // Invalid argument
    case ENOMEM:       // Out of memory
    case ENOENT:       // No such file or directory
    case ENODEV:       // No such device
    case ENXIO:        // No such device or address
    case EACCES:       // Permission denied
    case EPERM:        // Operation not permitted
    case ENOTDIR:      // Not a directory
    case EISDIR:       // Is a directory
    case ENAMETOOLONG: // File name too long
    case ELOOP:        // Too many symbolic links
    case EMFILE:       // Process limit for file descriptors reached
    case ENFILE:       // System-wide file descriptor table full
    case ENOTTY:       // Not a terminal
    case EBADF:        // Bad file descriptor
    case EAGAIN:       // Resource temporarily unavailable
    case EIO:          // Low-level I/O error
    case EBUSY:        // Device or resource busy
      return Severity::FATAL;
    case EINTR: // Call was interrupted by a signal
      return Severity::SHUTDOWN;
    default:
      return Severity::TRANSIENT;
    }
  }
};

#endif /* METER_ERROR_H_ */