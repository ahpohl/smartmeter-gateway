/**
 * @file modbus_error.h
 * @brief Defines Modbus error representation and severity classification.
 *
 * @details
 * Provides a struct to encapsulate Modbus errors, including the numeric error
 * code, human-readable message, and severity (transient vs. fatal). Factory
 * methods allow creation from system errno or custom error codes, and automatic
 * translation via modbus_strerror() ensures clear diagnostic output.
 */

#ifndef MODBUS_ERROR_H_
#define MODBUS_ERROR_H_

#include <cerrno>
#include <expected>
#include <format>
#include <modbus/modbus.h>
#include <string>

/**
 * @struct ModbusError
 * @brief Encapsulates a Modbus error with code, message, and severity.
 *
 * @details
 * This struct standardizes error handling for Modbus operations.
 * The severity indicates whether an error is transient (retryable) or fatal
 * (requires intervention). Factory methods support creation from errno or
 * explicit codes, and the `toString()` method combines both context and
 * Modbus-specific information in human-readable form.
 */
struct ModbusError {
public:
  /** @brief Error severity classification. */
  enum class Severity {
    TRANSIENT, /**< Temporary error — may succeed on retry. */
    FATAL,     /**< Fatal error — requires intervention. */
    SHUTDOWN   /**< Signal shutdown in progress */
  };

  /** @brief Modbus or system error code (as set in `errno`). */
  int code;

  /** @brief Contextual human-readable message (e.g. "Receive register 40329
   * failed"). */
  std::string message;

  /** @brief Classified severity of the error. */
  Severity severity;

  /**
   * @brief Create a ModbusError from the current system @c errno using a plain
   * message.
   *
   * This overload should be used when no formatting is required.
   * The message is stored as-is in the resulting ModbusError.
   *
   * @param msg Context message describing the error.
   * @return A ModbusError instance with @c code = errno and a severity deduced
   * via @c deduceSeverity().
   *
   * @see fromErrno(std::format_string<Args...>, Args&&...)
   * @see custom(int, const std::string&)
   *
   * @code
   * auto err = ModbusError::fromErrno("Failed to connect to Modbus device");
   * @endcode
   */
  static ModbusError fromErrno(const std::string &msg) {
    return {errno, msg, deduceSeverity(errno)};
  }

  /**
   * @brief Create a ModbusError from the current system @c errno using a
   * formatted message.
   *
   * This overload supports C++23-style @c std::format syntax for type-safe,
   * compile-time-checked formatting. The resulting message is formatted
   * according to the provided format string and arguments.
   *
   * @tparam Args Argument types deduced from the format string.
   * @param fmt Format string with {} placeholders (validated at compile time).
   * @param args Arguments to substitute into the format string.
   * @return A ModbusError instance with @c code = errno and a severity deduced
   * via @c deduceSeverity().
   *
   * @see fromErrno(const std::string&)
   * @see custom(int, std::format_string<Args...>, Args&&...)
   *
   * @code
   * auto err = ModbusError::fromErrno("Failed to read register {} from {}",
   * 40261, "inverter-1");
   * @endcode
   */
  template <typename... Args>
  static ModbusError fromErrno(std::format_string<Args...> fmt,
                               Args &&...args) {
    return {errno, std::format(fmt, std::forward<Args>(args)...),
            deduceSeverity(errno)};
  }

  /**
   * @brief Create a ModbusError with a custom error code and a plain message.
   *
   * This overload is used when the error code is not derived from @c errno,
   * but is manually provided by the caller. The message is stored as-is.
   *
   * @param c Custom error code.
   * @param msg Context message describing the error.
   * @return A ModbusError instance with the given code and deduced severity.
   *
   * @see custom(int, std::format_string<Args...>, Args&&...)
   * @see fromErrno(const std::string&)
   *
   * @code
   * auto err = ModbusError::custom(1234, "Invalid Modbus address");
   * @endcode
   */
  static ModbusError custom(int c, const std::string &msg) {
    return {c, msg, deduceSeverity(c)};
  }

  /**
   * @brief Create a ModbusError with a custom error code and a formatted
   * message.
   *
   * This overload supports C++23-style @c std::format syntax for compile-time
   * checked formatting. The formatted message is constructed with the provided
   * format string and arguments.
   *
   * @tparam Args Argument types deduced from the format string.
   * @param code Custom error code.
   * @param fmt Format string with {} placeholders (validated at compile time).
   * @param args Arguments to substitute into the format string.
   * @return A ModbusError instance with the given code and deduced severity.
   *
   * @see custom(int, const std::string&)
   * @see fromErrno(std::format_string<Args...>, Args&&...)
   *
   * @code
   * auto err = ModbusError::custom(1002, "Register {} invalid for {}", 40001,
   * "hybrid inverter");
   * @endcode
   */
  template <typename... Args>
  static ModbusError custom(int code, std::format_string<Args...> fmt,
                            Args &&...args) {
    return {code, std::format(fmt, std::forward<Args>(args)...),
            deduceSeverity(code)};
  }

  /**
   * @brief Unwraps a std::expected<T, ModbusError> or throws the contained
   * ModbusError.
   *
   * This helper function simplifies error handling by allowing direct
   * extraction of the expected value, while automatically throwing the error if
   * the operation failed. It is particularly useful for simplifying code that
   * would otherwise need to manually check `res.has_value()` and handle the
   * error path separately.
   *
   * @tparam T  The value type contained in the std::expected.
   * @param res The std::expected<T, ModbusError> result to unwrap.
   *
   * @return The unwrapped value of type T, if the operation succeeded.
   *
   * @throws ModbusError if the expected contains an error instead of a value.
   *
   * @note This function should be used in contexts where throwing a ModbusError
   *       is acceptable (e.g. within a try/catch block). For non-throwing code
   *       paths, handle the std::expected manually instead.
   *
   * @see std::expected
   * @see ModbusError
   */
  template <typename T> static T getOrThrow(std::expected<T, ModbusError> res) {
    if (!res)
      throw res.error();

    if constexpr (!std::is_void_v<T>) {
      return *res;
    }
  }

  /**
   * @brief Get a preformatted human-readable error description.
   *
   * @details
   * Formats the error as: "<message>: <libmodbus_text> (code <code>)".
   * The libmodbus text comes from modbus_strerror(code).
   *
   * @return An owning std::string with the formatted description.
   */
  std::string describe() const {
    return std::format("{}: {} (code {})", message, modbus_strerror(code),
                       code);
  }

private:
  /**
   * @brief Deduce severity based on the error code.
   * @param c Error code (errno or custom).
   * @return Severity::FATAL for unrecoverable errors; otherwise TRANSIENT.
   */
  static Severity deduceSeverity(int c) {
    switch (c) {
    case EINVAL:    // Invalid argument
    case ENOMEM:    // Out of memory
    case ENOENT:    // No such file or directory
    case EMBMDATA:  // Too many registers requested
    case EMBXILFUN: // Illegal function
    case EMBXILADD: // Illegal data address
    case EMBXILVAL: // Illegal data value
    case EMBXSFAIL: // Slave device or server failure
    case EMBXGTAR:  // Gateway target device failed to respond
      return Severity::FATAL;
    case EINTR: // Call was interrupted by a signal
      return Severity::SHUTDOWN;
    default:
      return Severity::TRANSIENT;
    }
  }
};

#endif /* MODBUS_ERROR_H_ */