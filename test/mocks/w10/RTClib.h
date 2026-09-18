#pragma once

#include <cstdint>
#include <ctime>

// Use the host clock to test RTC register serialization.
class DateTime {
  std::tm fields{};
public:
  inline static unsigned component_constructions = 0;
  DateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    ++component_constructions;
    fields.tm_year = year - 1900;
    fields.tm_mon = month - 1;
    fields.tm_mday = day;
    fields.tm_hour = hour;
    fields.tm_min = minute;
    fields.tm_sec = second;
  }
  explicit DateTime(uint32_t value) {
    time_t time = value;
    gmtime_r(&time, &fields);
  }
  uint32_t unixtime() const { auto value = fields; return timegm(&value); }
  bool isValid() const {
    auto value = fields;
    timegm(&value);
    return value.tm_year == fields.tm_year && value.tm_mon == fields.tm_mon
        && value.tm_mday == fields.tm_mday && value.tm_hour == fields.tm_hour
        && value.tm_min == fields.tm_min && value.tm_sec == fields.tm_sec;
  }
  uint16_t year() const { return fields.tm_year + 1900; }
  uint8_t month() const { return fields.tm_mon + 1; }
  uint8_t day() const { return fields.tm_mday; }
  uint8_t hour() const { return fields.tm_hour; }
  uint8_t minute() const { return fields.tm_min; }
  uint8_t second() const { return fields.tm_sec; }
  uint8_t dayOfTheWeek() const { auto value = fields; timegm(&value); return value.tm_wday; }
};
