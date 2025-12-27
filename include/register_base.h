#ifndef REGISTER_BASE_H_
#define REGISTER_BASE_H_

#include <cstdint>

#include <cstdint>
#include <format>
#include <string>

/**
 * @struct Register
 * @brief Represents a Modbus register definition.
 *
 * @details
 * Each instance of `Register` specifies the register address (`ADDR`),
 * the number of consecutive 16-bit registers (`NB`) used to store the value,
 * and the type of data (`TYPE`). This allows higher-level functions to
 * interpret raw Modbus data correctly, whether it is integer, floating-point,
 * or string data.
 */
struct Register {
  /**
   * @enum Type
   * @brief Enumerates the possible types of values stored in Modbus registers.
   *
   * @details
   * This enum allows the code to explicitly distinguish between
   * different kinds of register contents, such as signed vs. unsigned integers,
   * floating-point values, or strings. It improves type safety and readability.
   */
  enum class Type : uint8_t {
    UINT16, /**< 16-bit unsigned integer */
    INT16,  /**< 16-bit signed integer */
    UINT32, /**< 32-bit unsigned integer (two consecutive 16-bit registers) */
    UINT64, /**< 64-bit unsigned integer (four consecutive 16-bit registers) */
    FLOAT,  /**< 32-bit floating-point value (single-precision) */
    STRING, /**< ASCII string stored across multiple 16-bit registers */
    UNKNOWN /**< RegType not specified or unknown */
  };

  uint16_t ADDR{0}; /**< Modbus register address (0-based or device-specific) */
  uint16_t NB{0};   /**< Number of consecutive registers used for this value */
  Type TYPE{Type::UNKNOWN}; /**< RegType of value stored in the register */

  /**
   * @brief Construct a register definition.
   * @param addr Starting register address.
   * @param nb Number of consecutive 16-bit registers used for this value.
   * @param type RegType of value stored in the register.
   */
  constexpr Register(uint16_t addr, uint16_t nb, Type type)
      : ADDR(addr), NB(nb), TYPE(type) {}

  /**
   * @brief Return a copy of this register with an address offset applied.
   * @param offset Offset to add to the register address (can be negative).
   * @return A new Register instance with the adjusted address.
   */
  [[nodiscard]] constexpr Register withOffset(int16_t offset) const {
    return Register{static_cast<uint16_t>(ADDR + offset), NB, TYPE};
  }

  /**
   * @brief Convert a register Type enum value to a human-readable string.
   * @param type The Type value to convert.
   * @return A C-string representing the enum name.
   */
  static constexpr const char *typeToString(Type type) {
    switch (type) {
    case Type::UINT16:
      return "UINT16";
    case Type::INT16:
      return "INT16";
    case Type::UINT32:
      return "UINT32";
    case Type::UINT64:
      return "UINT64";
    case Type::FLOAT:
      return "FLOAT";
    case Type::STRING:
      return "STRING";
    case Type::UNKNOWN:
      return "UNKNOWN";
    default:
      return "INVALID";
    }
  }

  /**
   * @brief Return a human-readable string representation of the register.
   *
   * @details
   * The returned string includes the register address, number of consecutive
   * registers, and the type as a string. Useful for logging and error messages.
   *
   * @return std::string in the format: "[ADDR=..., NB=..., TYPE=...]"
   */
  std::string describe() const {
    return std::format("[ADDR={}, NB={}, TYPE={}]", ADDR, NB,
                       typeToString(TYPE));
  }
};

#endif /* REGISTER_BASE_H_ */
