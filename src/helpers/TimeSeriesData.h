#pragma once

#include <stdint.h>

namespace mesh {
class RTCClock;
}

struct MinMaxAvg {
  float _min, _max, _avg;
  uint8_t _lpp_type, _channel;
};

class TimeSeriesData {
  float *data;
  int num_slots, next, count;
  uint32_t last_timestamp, interval_secs;
  bool owns_data;

public:
  TimeSeriesData(float *array, int num, uint32_t secs);
  TimeSeriesData(int num, uint32_t secs) : TimeSeriesData(nullptr, num, secs) {}
  ~TimeSeriesData();
  TimeSeriesData(const TimeSeriesData &) = delete;
  TimeSeriesData &operator=(const TimeSeriesData &) = delete;

  // Caller supplies monotonic seconds, accumulated across millisecond clock wraps.
  void recordData(uint32_t now_secs, float value);
  void calcMinMaxAvg(uint32_t now_secs, uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg *dest,
                     uint8_t channel, uint8_t lpp_type) const;

  // Compatibility for custom sensor sketches. The RTC argument is ignored.
  [[deprecated("Pass monotonic seconds instead of RTCClock*")]]
  void recordData(mesh::RTCClock *clock, float value);
  [[deprecated("Pass monotonic seconds instead of RTCClock*")]]
  void calcMinMaxAvg(mesh::RTCClock *clock, uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg *dest,
                     uint8_t channel, uint8_t lpp_type) const;
};
