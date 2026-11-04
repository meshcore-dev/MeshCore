#pragma once

#include <stdint.h>

// Minimal DateTime stand-in for native tests. unixtime() matches RTClib for
// valid dates from 2000 to 2099, the range RTClib supports (seconds since
// 1970-01-01 UTC). Out-of-range fields map to a deterministic value instead of
// indexing past the month table, so tests can compare two parses of the same
// bytes; such values are not what RTClib would return.
class DateTime {
  uint16_t _year;
  uint8_t _month, _day, _hour, _minute, _second;

  static bool leap(uint16_t year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  }

public:
  DateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
           uint8_t minute, uint8_t second)
      : _year(year), _month(month), _day(day), _hour(hour),
        _minute(minute), _second(second) {}

  uint32_t unixtime() const {
    uint32_t days = 0;
    for (uint16_t year = 1970; year < _year; ++year)
      days += leap(year) ? 366 : 365;
    if (_month >= 1 && _month <= 12) {
      const uint8_t month_days[] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
      for (uint8_t month = 1; month < _month; ++month)
        days += month_days[month - 1] + (month == 2 && leap(_year) ? 1 : 0);
      days += _day - 1;
    } else {
      days += 31UL * _month + _day;  // deterministic, not calendar-correct
    }
    return days * 86400UL + _hour * 3600UL + _minute * 60UL + _second;
  }
};
