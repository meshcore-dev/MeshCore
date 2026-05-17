#pragma once

#include <Arduino.h>
#include <Mesh.h>

struct MinMaxAvg {
  float _min, _max, _avg;
  uint8_t _lpp_type, _channel;
};

class TimeSeriesData {
  float* data;
  int num_slots, next;
  uint32_t last_timestamp;
  uint32_t interval_secs;

public:
  TimeSeriesData(float* array, int num, uint32_t secs) : num_slots(num), data(array), last_timestamp(0), next(0), interval_secs(secs) { 
    memset(data, 0, sizeof(float)*num);
  }
  TimeSeriesData(int num, uint32_t secs) : num_slots(num), last_timestamp(0), next(0), interval_secs(secs) {
    data = new float[num];
    memset(data, 0, sizeof(float)*num);
  }

  void recordData(mesh::RTCClock* clock, float value);
  void calcMinMaxAvg(mesh::RTCClock* clock, uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg* dest, uint8_t channel, uint8_t lpp_type) const;
};

