/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "BoardTelemetry.h"

#include "../BoardConfigContainer.h"
#include <CayenneLPP.h>
#include <math.h>

namespace inhero {

namespace {

uint8_t getLPPDataLength(uint8_t type) {
  switch (type) {
  case LPP_DIGITAL_INPUT:
  case LPP_DIGITAL_OUTPUT:
  case LPP_PRESENCE:
  case LPP_RELATIVE_HUMIDITY:
  case LPP_PERCENTAGE:
  case LPP_SWITCH:
    return 1;
  case LPP_ANALOG_INPUT:
  case LPP_ANALOG_OUTPUT:
  case LPP_LUMINOSITY:
  case LPP_TEMPERATURE:
  case LPP_BAROMETRIC_PRESSURE:
  case LPP_VOLTAGE:
  case LPP_CURRENT:
  case LPP_ALTITUDE:
  case LPP_POWER:
  case LPP_DIRECTION:
  case LPP_CONCENTRATION:
    return 2;
  case LPP_COLOUR:
    return 3;
  case LPP_GENERIC_SENSOR:
  case LPP_FREQUENCY:
  case LPP_DISTANCE:
  case LPP_ENERGY:
  case LPP_UNIXTIME:
    return 4;
  case LPP_ACCELEROMETER:
  case LPP_GYROMETER:
    return 6;
  case LPP_GPS:
    return 9;
  case LPP_POLYLINE:
    return 8; // minimum size
  default:
    return 0;
  }
}

uint8_t findNextFreeLppChannel(CayenneLPP& lpp) {
  uint8_t max_channel = 0;
  uint8_t cursor = 0;
  uint8_t* buffer = lpp.getBuffer();
  uint8_t size = lpp.getSize();

  while (cursor < size) {
    if (cursor + 1 >= size) break;
    uint8_t channel = buffer[cursor];
    uint8_t type = buffer[cursor + 1];
    uint8_t data_len = getLPPDataLength(type);
    if (data_len == 0) break;  // unknown type, can't continue
    if (channel > max_channel) max_channel = channel;
    cursor += 2 + data_len;
  }
  return max_channel + 1;
}

} // namespace

bool appendBoardTelemetry(BoardConfigContainer& cfg, CayenneLPP& telemetry) {
  const Telemetry* telemetryData = cfg.getTelemetryData();
  if (!telemetryData) return false;

  uint8_t batteryChannel = findNextFreeLppChannel(telemetry);
  uint8_t solarChannel = batteryChannel + 1;

  const BatterySOCStats* socStats = cfg.getSOCStats();
  bool hasValidSoc = (socStats && socStats->soc_valid);
  float socPercent = roundf(cfg.getStateOfCharge() * 10.0f) / 10.0f;

  uint16_t ttlHours = cfg.getTTL_Hours();
  bool isInfiniteTtl = (socStats && socStats->soc_valid && !socStats->living_on_battery);
  constexpr float MAX_TTL_DAYS = 990.0f;  // sentinel reported when TTL is effectively infinite

  // Battery: VBAT[V], SOC[%] (opt), IBAT[A], TBAT[°C], TTL[d] (opt)
  telemetry.addVoltage(batteryChannel, telemetryData->battery.voltage / 1000.0f);
  if (hasValidSoc) telemetry.addPercentage(batteryChannel, socPercent);
  telemetry.addCurrent(batteryChannel, telemetryData->battery.current / 1000.0f);
  if (telemetryData->battery.temperature > -100.0f) {
    telemetry.addTemperature(batteryChannel, telemetryData->battery.temperature);
  }
  if (ttlHours > 0) {
    telemetry.addDistance(batteryChannel, ttlHours / 24.0f);
  } else if (isInfiniteTtl) {
    telemetry.addDistance(batteryChannel, MAX_TTL_DAYS);
  }

  // Solar: VSOL[V], ISOL[A], MPPT_7D[%]
  if (telemetryData->solar.valid) {
    telemetry.addVoltage(solarChannel, telemetryData->solar.voltage / 1000.0f);
    telemetry.addCurrent(solarChannel, telemetryData->solar.current / 1000.0f);
  }
  telemetry.addPercentage(solarChannel, cfg.getMpptEnabledPercentage7Day());

  return true;
}

} // namespace inhero
