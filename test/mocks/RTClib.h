#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

// Just enough of RTClib's DateTime for GpsGuard: the firmware build date as an epoch.
class DateTime {
  uint32_t _unix = 0;

public:
  // __DATE__ ("Mmm dd yyyy") and __TIME__ ("hh:mm:ss")
  DateTime(const char* date, const char* time) {
    static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int mon = 1;
    for (int i = 0; i < 12; ++i) if (strncmp(date, months + 3 * i, 3) == 0) mon = i + 1;
    const int day = atoi(date + 4), year = atoi(date + 7);
    // days from civil (Howard Hinnant)
    const int y = year - (mon <= 2);
    const int era = y / 400, yoe = y - era * 400;
    const int doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = (long)era * 146097 + doe - 719468;
    _unix = (uint32_t)(days * 86400L + atoi(time) * 3600L + atoi(time + 3) * 60L + atoi(time + 6));
  }
  uint32_t unixtime() const { return _unix; }
};
