#ifndef MODBUS_UTILS_H_
#define MODBUS_UTILS_H_

#include "modbus_error.h"
#include "register_base.h"
#include <arpa/inet.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>
#include <modbus/modbus.h>
#include <netinet/in.h>
#include <string>

namespace detail {

// --- Pack integral value into Modbus registers ---
// Internal helper function - do not call directly, use packToModbus instead
template <typename T> void packInteger(uint16_t *dest, T value) {
  static_assert(std::is_integral_v<T>, "T must be an integral type");

  constexpr int nBytes = sizeof(T);
  constexpr int nWords = nBytes / 2;

  unsigned char buf[nBytes];
  std::memcpy(buf, &value, nBytes);

  for (int i = 0; i < nWords; i++) {
    unsigned char hi = buf[2 * i + 1];
    unsigned char lo = buf[2 * i];
    dest[i] = (static_cast<uint16_t>(hi) << 8) | lo;
  }

  std::reverse(dest, dest + nWords);
}

}; // namespace detail

namespace ModbusUtils {

// --- pack integer, string and float values into Modbus registers ---
template <typename T>
std::expected<void, ModbusError> packToModbus(modbus_mapping_t *dest,
                                              Register reg, T value) {

  if (!dest) {
    return std::unexpected(
        ModbusError::custom(EINVAL, "Null modbus_mapping_t pointer"));
  }
  if (!dest->tab_registers) {
    return std::unexpected(
        ModbusError::custom(EINVAL, "modbus_mapping_t has null tab_registers"));
  }

  switch (reg.TYPE) {
  case Register::Type::INT16:
    if constexpr (std::is_integral_v<T>)
      dest->tab_registers[reg.ADDR] = static_cast<uint16_t>(value);
    break;
  case Register::Type::UINT16:
    if constexpr (std::is_integral_v<T>)
      dest->tab_registers[reg.ADDR] = value;
    break;
  case Register::Type::UINT32:
    if constexpr (std::is_integral_v<T>)
      detail::packInteger<uint32_t>(&dest->tab_registers[reg.ADDR], value);
    break;
  case Register::Type::UINT64:
    if constexpr (std::is_integral_v<T>)
      detail::packInteger<uint64_t>(&dest->tab_registers[reg.ADDR], value);
    break;
  case Register::Type::FLOAT:
    if constexpr (std::is_floating_point_v<T>)
      modbus_set_float_abcd(static_cast<float>(value),
                            &dest->tab_registers[reg.ADDR]);
    break;
  case Register::Type::STRING: {
    if constexpr (std::is_same_v<T, std::string>) {
      size_t maxLength = reg.NB * 2;
      if (value.length() > maxLength) {
        return std::unexpected(ModbusError::custom(
            EINVAL,
            "String length {} exceeds maximum {} characters for register {}",
            value.length(), maxLength, reg.describe()));
      }

      // Pack string into modbus mapping
      for (size_t i = 0; i < value.length() / 2; i++) {
        unsigned char hi = value[2 * i];
        unsigned char lo = value[2 * i + 1];
        dest->tab_registers[reg.ADDR + i] =
            (static_cast<uint16_t>(hi) << 8) | lo;
      }
      if (value.length() % 2) {
        dest->tab_registers[reg.ADDR + value.length() / 2] =
            (static_cast<uint16_t>(value[value.length() - 1]) << 8);
      }

      // Zero out remaining registers
      for (size_t i = (value.length() + 1) / 2; i < static_cast<size_t>(reg.NB);
           i++) {
        dest->tab_registers[reg.ADDR + i] = 0;
      }
    }
    break;
  }
  case Register::Type::UNKNOWN:
  default:
    return std::unexpected(ModbusError::custom(
        EINVAL, std::format("Unsupported target register type: {}",
                            Register::typeToString(reg.TYPE))));
  }

  return {};
}

// --- encode a double value into integer + scale factor registers ---
inline std::expected<void, ModbusError> packToModbus(modbus_mapping_t *dest,
                                                     Register reg, Register sf,
                                                     double realValue,
                                                     int decimals) {
  if (!dest) {
    return std::unexpected(
        ModbusError::custom(EINVAL, "Null modbus_mapping_t pointer"));
  }
  if (!dest->tab_registers) {
    return std::unexpected(
        ModbusError::custom(EINVAL, "modbus_mapping_t has null tab_registers"));
  }

  // encode float into integer register
  switch (reg.TYPE) {
  case Register::Type::INT16: {
    int16_t value =
        static_cast<int16_t>(std::round(realValue * std::pow(10, decimals)));
    dest->tab_registers[reg.ADDR] = static_cast<uint16_t>(value);
    break;
  }
  case Register::Type::UINT16: {
    uint16_t value =
        static_cast<uint16_t>(std::round(realValue * std::pow(10, decimals)));
    dest->tab_registers[reg.ADDR] = value;
    break;
  }

  case Register::Type::UINT32: {
    uint32_t value =
        static_cast<uint32_t>(std::round(realValue * std::pow(10, decimals)));
    detail::packInteger<uint32_t>(&dest->tab_registers[reg.ADDR], value);
    break;
  }
  case Register::Type::UINT64: {
    uint64_t value =
        static_cast<uint64_t>(std::round(realValue * std::pow(10, decimals)));
    detail::packInteger<uint64_t>(&dest->tab_registers[reg.ADDR], value);
    break;
  }

  default:
    return std::unexpected(ModbusError::custom(
        EINVAL,
        std::format("Unsupported target register type for encoding float: {}",
                    Register::typeToString(reg.TYPE))));
  }

  // encode scale factor (scale factors are INT16 in SunSpec)
  int16_t sf_value = -static_cast<int16_t>(decimals);
  dest->tab_registers[sf.ADDR] = static_cast<uint16_t>(sf_value);

  return {};
}

inline std::pair<std::string, int> getClientInfo(int socket) {
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);

  if (getpeername(socket, (struct sockaddr *)&addr, &addr_len) != 0) {
    return {"unknown", 0};
  }

  char ip_str[INET6_ADDRSTRLEN];
  int port = 0;

  if (addr.ss_family == AF_INET) {
    // IPv4
    struct sockaddr_in *addr_in = (struct sockaddr_in *)&addr;
    inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));
    port = ntohs(addr_in->sin_port);
  } else if (addr.ss_family == AF_INET6) {
    // IPv6
    struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&addr;
    inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, sizeof(ip_str));
    port = ntohs(addr_in6->sin6_port);
  } else {
    return {"unknown", 0};
  }

  return {ip_str, port};
}

} // namespace ModbusUtils

#endif /* MODBUS_UTILS_H_ */
