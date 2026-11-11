#include "TimeSeriesData.h"

#include <Arduino.h>
#include <math.h>

TimeSeriesData::TimeSeriesData(float *array, int num, uint32_t secs)
    : data(array), num_slots(num > 0 ? num : 0), next(0), count(0), last_timestamp(0), interval_secs(secs),
      owns_data(array == nullptr) {
  if (num_slots > 0) {
    if (owns_data) data = new float[num_slots];
  }
}

TimeSeriesData::~TimeSeriesData() {
  if (owns_data) delete[] data;
}

void TimeSeriesData::recordData(uint32_t now_secs, float value) {
  if (num_slots == 0 || isnan(value)) return;
  uint32_t steps = 1;
  if (count > 0 && interval_secs > 0) {
    uint32_t elapsed = now_secs - last_timestamp;
    if (elapsed < interval_secs) return;
    steps = elapsed / interval_secs;
    last_timestamp += steps * interval_secs; // keep a fixed sampling grid
  } else {
    last_timestamp = now_secs;
  }

  // A long pause expires the whole ring in O(capacity), never O(elapsed time).
  if (steps >= uint32_t(num_slots)) {
    next = count = 0;
  } else {
    for (uint32_t i = 1; i < steps; i++) {
      data[next] = NAN;
      next = (next + 1) % num_slots;
      if (count < num_slots) count++;
    }
  }

  data[next] = value;
  next = (next + 1) % num_slots;
  if (count < num_slots) count++;
}

void TimeSeriesData::calcMinMaxAvg(uint32_t now_secs, uint32_t start_secs_ago, uint32_t end_secs_ago,
                                   MinMaxAvg *dest, uint8_t channel, uint8_t lpp_type) const {
  dest->_channel = channel;
  dest->_lpp_type = lpp_type;
  dest->_min = dest->_max = dest->_avg = NAN;
  int num_values = 0;
  float total = 0.0f;
  int i = next;
  uint64_t ago = uint32_t(now_secs - last_timestamp);
  for (int n = 0; n < count; n++) {
    i = (i + num_slots - 1) % num_slots;
    if (ago >= end_secs_ago && ago < start_secs_ago && !isnan(data[i])) {
      float v = data[i];
      if (num_values == 0) {
        dest->_min = dest->_max = v;
      } else {
        if (v < dest->_min) dest->_min = v;
        if (v > dest->_max) dest->_max = v;
      }
      total += v;
      num_values++;
    }
    ago += interval_secs;
  }
  if (num_values > 0) dest->_avg = total / num_values;
}

// Shared only by legacy callers; never consult the RTC, even after a time sync.
// At least one legacy call is required per millis() wrap (about 49 days).
static uint32_t legacyMonotonicSeconds() {
  static uint32_t last_millis = 0;
  static uint64_t uptime_millis = 0;
  uint32_t now = millis();
  uptime_millis += uint32_t(now - last_millis);
  last_millis = now;
  return uptime_millis / 1000;
}

void TimeSeriesData::recordData(mesh::RTCClock *, float value) {
  recordData(legacyMonotonicSeconds(), value);
}

void TimeSeriesData::calcMinMaxAvg(mesh::RTCClock *, uint32_t start_secs_ago, uint32_t end_secs_ago,
                                   MinMaxAvg *dest, uint8_t channel, uint8_t lpp_type) const {
  calcMinMaxAvg(legacyMonotonicSeconds(), start_secs_ago, end_secs_ago, dest, channel, lpp_type);
}
