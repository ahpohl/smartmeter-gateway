#ifndef METER_TYPES_H_
#define METER_TYPES_H_

#include <cstdint>
#include <string>
#include <termios.h>

struct MeterTypes {

  struct Phase {
    double voltage{0.0};
    double power{0.0};
  };

  struct Values {
    uint64_t time{0};
    uint64_t activeSensorTime{0};
    double energy{0.0};
    double power{0.0};
    Phase phase1;
    Phase phase2;
    Phase phase3;
  };

  struct Device {
    std::string manufacturer;
    std::string model;
    std::string serialNumber;
    std::string fwVersion;
    std::string status;
    int phases{0};
  };

  enum class ErrorAction { NONE, RECONNECT, SHUTDOWN };

  // Helper function to convert baud rate integer to termios speed constant
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

  // Helper function to convert data bits to termios constant
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
};

#endif /* METER_TYPES_H_ */
