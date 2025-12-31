#ifndef METER_TYPES_H_
#define METER_TYPES_H_

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <termios.h>

struct MeterTypes {

  // --- Serial parity configuration ---
  enum class Parity { None, Even, Odd };

  // --- Preset types for serial communication ---
  enum class Preset {
    OdType, // Optical interface:  9600 7E1
    SdType  // Multi functional interface: 9600 8N1
  };

  // --- Resolved serial parameters ---
  struct SerialParams {
    int baud{9600};
    int dataBits{8};
    int stopBits{1};
    Parity parity{Parity::None};
  };

  // --- Meter value types ---
  struct Phase {
    double voltage{0.0};
    double current{0.0};
    double activePower{0.0};
    double reactivePower{0.0};
    double apparentPower{0.0};
    double powerFactor{0.0};
  };

  struct Values {
    uint64_t time{0};
    uint64_t activeSensorTime{0};
    double energy{0.0};
    double voltage{0.0};
    double current{0.0};
    double activePower{0.0};
    double reactivePower{0.0};
    double apparentPower{0.0};
    double powerFactor{0.0};
    Phase phase1;
    Phase phase2;
    Phase phase3;
  };

  struct Device {
    std::string manufacturer;
    std::string model;
    std::string options;
    std::string serialNumber;
    std::string fwVersion;
    std::string status;
    int phases{0};
  };

  enum class ErrorAction { NONE, RECONNECT, SHUTDOWN };

  // --- Parse functions ---
  static Parity parseParity(const std::string &val) {
    if (val == "none")
      return Parity::None;
    if (val == "even")
      return Parity::Even;
    if (val == "odd")
      return Parity::Odd;
    throw std::invalid_argument("parity must be one of: none, even, odd");
  }

  static std::optional<Preset> parsePreset(const std::string &val) {
    if (val == "od_type")
      return Preset::OdType;
    if (val == "sd_type")
      return Preset::SdType;
    throw std::invalid_argument("preset must be one of: od_type, sd_type");
  }

  // --- Get default parameters for a preset ---
  static SerialParams getPresetDefaults(Preset preset) {
    switch (preset) {
    case Preset::OdType:
      return {9600, 7, 1, Parity::Even};
    case Preset::SdType:
      return {9600, 8, 1, Parity::None};
    }
    return {};
  }

  // --- Conversion helpers ---
  static char parityToChar(Parity parity) {
    switch (parity) {
    case Parity::Even:
      return 'E';
    case Parity::Odd:
      return 'O';
    default:
      return 'N';
    }
  }

  static speed_t baudToSpeed(int baud) {
    switch (baud) {
    case 1200:
      return B1200;
    case 2400:
      return B2400;
    case 4800:
      return B4800;
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
    case 230400:
      return B230400;
    case 460800:
    default:
      return B9600;
    }
  }

  static tcflag_t dataBitsToFlag(int dataBits) {
    switch (dataBits) {
    case 5:
      return CS5;
    case 6:
      return CS6;
    case 7:
      return CS7;
    case 8:
      return CS8;
    default:
      return CS8;
    }
  }

  static void applyParity(termios &tty, Parity parity) {
    switch (parity) {
    case Parity::None:
      tty.c_cflag &= ~PARENB;
      break;
    case Parity::Even:
      tty.c_cflag |= PARENB;
      tty.c_cflag &= ~PARODD;
      break;
    case Parity::Odd:
      tty.c_cflag |= PARENB;
      tty.c_cflag |= PARODD;
      break;
    }
  }
};

#endif /* METER_TYPES_H_ */