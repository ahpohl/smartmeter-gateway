#ifndef MODBUS_UTILS_H_
#define MODBUS_UTILS_H_

#include "meter_types.h"
#include "modbus_error.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <expected>

namespace ModbusUtils {

// --- Helper: pack string into Modbus registers ---
inline std::expected<void, ModbusError> stringToModbus(uint16_t *dest,
                                                       std::string src) {
  if (src.length() > 32) {
    return std::unexpected(ModbusError::custom(
        EINVAL,
        "stringToModbus(): String length {} exceeds maximum "
        "of 32 characters for String32 register.",
        src.length()));
  }
  for (int i = 0; i < src.length() / 2; i++) {
    unsigned char hi = src[2 * i];
    unsigned char lo = src[2 * i + 1];
    dest[i] = (hi << 8) | lo;
  }
  if (src.length() % 2) {
    dest[src.length() / 2] = (src[src.length() - 1] << 8);
  }

  return {};
}

// --- Pack integral value into Modbus registers ---
template <typename T> void packToModbus(uint16_t *dest, T value) {
  static_assert(std::is_integral_v<T>, "T must be an integral type");

  if constexpr (std::is_same_v<T, int16_t>) {
    *dest = static_cast<uint16_t>(value);
  } else if constexpr (std::is_same_v<T, uint16_t>) {
    *dest = value;
  } else {
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
}

} // namespace ModbusUtils

#endif /* MODBUS_UTILS_H_ */
