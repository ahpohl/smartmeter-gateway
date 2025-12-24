#ifndef METER_TYPES_H_
#define METER_TYPES_H_

#include <cstdint>
#include <string>

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
};

#endif /* METER_TYPES_H_ */
