#pragma once

#include <stdint.h>

namespace mesh {

struct RssiCarrierSenseConfig {
  int16_t noise_floor;
  int16_t threshold;
  bool noise_floor_valid;
  uint8_t required_busy_samples;
};

class RssiCarrierSense {
public:
  static bool isActive(const RssiCarrierSenseConfig& config, const int16_t samples[], uint8_t sample_count) {
    if (config.threshold <= 0 || !config.noise_floor_valid || samples == nullptr || sample_count == 0) {
      return false;
    }

    uint8_t required = config.required_busy_samples;
    if (required == 0 || required > sample_count) {
      required = sample_count;
    }

    uint8_t busy_samples = 0;
    const int16_t busy_level = config.noise_floor + config.threshold;
    for (uint8_t i = 0; i < sample_count; i++) {
      if (samples[i] > busy_level) {
        busy_samples++;
        if (busy_samples >= required) {
          return true;
        }
      }
    }

    return false;
  }
};

}
