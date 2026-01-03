#ifndef JSON_UTILS_H_
#define JSON_UTILS_H_

#include <cmath>
#include <nlohmann/json.hpp>

namespace JsonUtils {

using json = nlohmann::ordered_json;

// --- Round the double to specified decimal places
inline double roundTo(double value, int decimals) {
  double factor = std::pow(10.0, decimals);
  return std::round(value * factor) / factor;
}

} // namespace JsonUtils

#endif /* JSON_UTILS_H_ */