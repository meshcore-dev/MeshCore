#pragma once

#include <stdint.h>

// Days since 1970-01-01 (Howard Hinnant's days_from_civil), for years >= 1970.
inline int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  int era = y / 400;
  unsigned yoe = y - era * 400;
  unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// EU clocks change on the last Sunday of March and October, at 01:00 UTC.
inline uint32_t euClockChangeUtc(int year, unsigned month) {
  int32_t day = daysFromCivil(year, month, 31);
  int weekday = (day + 4) % 7;  // 1970-01-01 was a Thursday, 0 = Sunday
  return (uint32_t)(day - weekday) * 86400 + 3600;
}

inline bool isEuSummerTime(uint32_t utc) {
  // Average year length: the year may be off by one only around New Year, in winter either way.
  int year = 1970 + utc / 31556952;
  return utc >= euClockChangeUtc(year, 3) && utc < euClockChangeUtc(year, 10);
}

// Single-cell LiPo charge, same curve as the meshcore-m5 app: one point every 5 %.
inline int lipoPercent(uint16_t mv) {
  static const uint16_t MV[] = {3270, 3610, 3690, 3710, 3730, 3750, 3770, 3790, 3800, 3820, 3840,
                                3850, 3870, 3910, 3950, 3980, 4020, 4080, 4110, 4150, 4200};
  if (mv < MV[0]) return 0;
  for (int i = 1; i < 21; i++) {
    if (mv < MV[i]) {
      int span = MV[i] - MV[i - 1];
      return (i - 1) * 5 + (10 * (mv - MV[i - 1]) + span) / (2 * span);
    }
  }
  return 100;
}
