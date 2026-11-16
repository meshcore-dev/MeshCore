#include "TimeSeriesData.h"

void TimeSeriesData::recordData(mesh::RTCClock* clock, float value) {
  uint32_t now = clock->getCurrentTime();
  if (now >= last_timestamp + interval_secs) {
    last_timestamp = now;

    data[next] = value;   // append to cycle table
    next = (next + 1) % num_slots;
  }
}

void TimeSeriesData::calcMinMaxAvg(mesh::RTCClock* clock, uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg* dest, uint8_t channel, uint8_t lpp_type) const {
  int i = next, n = num_slots;
  uint32_t ago = clock->getCurrentTime() - last_timestamp;
  int num_values = 0;
  float total = 0.0f;

  dest->_channel = channel;
  dest->_lpp_type = lpp_type;

  // start at most recet recording, back-track through to oldest
  while (n > 0) {
    n--;
    i = (i + num_slots - 1) % num_slots;  // go back by one
    if (ago >= end_secs_ago && ago < start_secs_ago) {   // filter by the desired time range
      float v = data[i];
      num_values++;
      total += v;
      if (num_values == 1) {
        dest->_max = dest->_min = v;
      } else {
        if (v < dest->_min) dest->_min = v;
        if (v > dest->_max) dest->_max = v;
      }
    }
    ago += interval_secs;
  }
  // calc average
  if (num_values > 0) {
    dest->_avg = total / num_values;
  } else {
    dest->_max = dest->_min = dest->_avg = NAN;
  }
}
