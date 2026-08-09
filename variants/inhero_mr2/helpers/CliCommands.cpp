/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "CliCommands.h"

#include "../BoardConfigContainer.h"

#include <CayenneLPP.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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

} // namespace

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
  telemetry.addVoltage(solarChannel, telemetryData->solar.voltage / 1000.0f);
  telemetry.addCurrent(solarChannel, telemetryData->solar.current / 1000.0f);
  telemetry.addPercentage(solarChannel, cfg.getMpptEnabledPercentage7Day());

  return true;
}

bool handleGet(BoardConfigContainer& cfg, const char* getCommand, char* reply, uint32_t maxlen) {
  // Trim trailing whitespace from command
  char trimmedCommand[100];
  strncpy(trimmedCommand, getCommand, sizeof(trimmedCommand) - 1);
  trimmedCommand[sizeof(trimmedCommand) - 1] = '\0';
  char* cmd = BoardConfigContainer::trim(trimmedCommand);

  if (strcmp(cmd, "bat") == 0) {
    snprintf(reply, maxlen, "%s",
             BoardConfigContainer::getBatteryTypeCommandString(cfg.getBatteryType()));
    return true;
  } else if (strcmp(cmd, "fmax") == 0) {
    const auto* props = BoardConfigContainer::getBatteryProperties(cfg.getBatteryType());
    if (props && props->ts_ignore) {
      snprintf(reply, maxlen, "N/A");
    } else {
      snprintf(reply, maxlen, "%s",
               BoardConfigContainer::getFrostChargeBehaviourCommandString(cfg.getFrostChargeBehaviour()));
    }
    return true;
  } else if (strcmp(cmd, "imax") == 0) {
    snprintf(reply, maxlen, "%s", cfg.getChargeCurrentAsStr());
    return true;
  } else if (strcmp(cmd, "mppt") == 0) {
    snprintf(reply, maxlen, "MPPT=%s", cfg.getMPPTEnabled() ? "1" : "0");
    return true;
  } else if (strcmp(cmd, "stats") == 0) {
    const BatterySOCStats* socStats = cfg.getSOCStats();
    if (!socStats) {
      snprintf(reply, maxlen, "N/A M:%.0f%%", cfg.getMpptEnabledPercentage7Day());
      return true;
    }

    // Rolling windows incl. current-hour accumulators (visible before first hour boundary)
    float last_24h_net = socStats->last_24h_net_mah
                       + socStats->current_hour_solar_mah
                       - socStats->current_hour_discharged_mah;
    float last_24h_charged = socStats->last_24h_charged_mah + socStats->current_hour_charged_mah;
    float last_24h_discharged = socStats->last_24h_discharged_mah + socStats->current_hour_discharged_mah;
    const char* status = socStats->living_on_battery ? "BAT" : "SOL";
    uint16_t ttl = cfg.getTTL_Hours();
    float mppt_pct = cfg.getMpptEnabledPercentage7Day();

    char ttlBuf[16];
    if (ttl >= 24) snprintf(ttlBuf, sizeof(ttlBuf), "%dd%dh", ttl / 24, ttl % 24);
    else if (ttl > 0) snprintf(ttlBuf, sizeof(ttlBuf), "%dh", ttl);
    else snprintf(ttlBuf, sizeof(ttlBuf), "N/A");

    snprintf(reply, maxlen,
             "%+.0f/%+.0f/%+.0fmAh C:%.0f D:%.0f 3C:%.0f 3D:%.0f 7C:%.0f 7D:%.0f %s M:%.0f%% T:%s",
             last_24h_net, socStats->avg_3day_daily_net_mah, socStats->avg_7day_daily_net_mah,
             last_24h_charged, last_24h_discharged,
             socStats->avg_3day_daily_charged_mah, socStats->avg_3day_daily_discharged_mah,
             socStats->avg_7day_daily_charged_mah, socStats->avg_7day_daily_discharged_mah,
             status, mppt_pct, ttlBuf);
    return true;
  } else if (strcmp(cmd, "cinfo") == 0) {
    char infoBuffer[100];
    cfg.getChargerInfo(infoBuffer, sizeof(infoBuffer));
    snprintf(reply, maxlen, "%s", infoBuffer);
    return true;
  } else if (strcmp(cmd, "bqdiag") == 0) {
    char diagBuffer[100];
    cfg.getBqDiagnostics(diagBuffer, sizeof(diagBuffer));
    snprintf(reply, maxlen, "%s", diagBuffer);
    return true;
  } else if (strcmp(cmd, "selftest") == 0) {
    char stBuffer[64];
    cfg.getSelfTest(stBuffer, sizeof(stBuffer));
    snprintf(reply, maxlen, "%s", stBuffer);
    return true;
  } else if (strcmp(cmd, "socdebug") == 0) {
    Ina228Driver* ina = cfg.getIna228Driver();
    if (!ina) {
      snprintf(reply, maxlen, "INA228 n/a");
      return true;
    }
    const BatterySOCStats* s = cfg.getSOCStats();
    uint16_t scal = ina->readShuntCalRegister();
    float chg = ina->readCharge_mAh();
    float cur = ina->readCurrent_mA_precise();
    uint32_t rtc = BoardConfigContainer::getRTCTimestamp();
    snprintf(reply, maxlen,
             "S=%u I=%.1f C=%.1f hC%.1f hD%.1f n=%u t=%lu d=%.2f",
             scal, cur, chg,
             s->current_hour_charged_mah, s->current_hour_discharged_mah,
             s->soc_update_count, (unsigned long)rtc, s->temp_derating_factor);
    return true;
  } else if (strcmp(cmd, "telem") == 0) {
    const Telemetry* telemetry = cfg.getTelemetryData();
    if (!telemetry) {
      snprintf(reply, maxlen, "Err: Telemetry unavailable");
      return true;
    }

    float precise_current_ma = telemetry->battery.current;
    float soc = cfg.getStateOfCharge();
    const BatterySOCStats* socStats = cfg.getSOCStats();

    // INA228 returns signed: positive=charging, negative=discharging
    char bat_current_str[16];
    snprintf(bat_current_str, sizeof(bat_current_str), "%.1fmA", precise_current_ma);

    char sol_current_str[16];
    int16_t sol_current = telemetry->solar.current;
    if (sol_current == 0)         snprintf(sol_current_str, sizeof(sol_current_str), "0mA");
    else if (sol_current < 50)    snprintf(sol_current_str, sizeof(sol_current_str), "<50mA");
    else if (sol_current <= 100)  snprintf(sol_current_str, sizeof(sol_current_str), "~%dmA", (int)sol_current);
    else                          snprintf(sol_current_str, sizeof(sol_current_str), "%dmA", (int)sol_current);

    char temp_str[8];
    if (telemetry->battery.temperature <= -100.0f) {
      snprintf(temp_str, sizeof(temp_str), "N/A");
    } else {
      snprintf(temp_str, sizeof(temp_str), "%.0fC", telemetry->battery.temperature);
    }

    if (socStats && socStats->soc_valid) {
      // Trapped Charge model: cold locks the bottom of the discharge curve.
      // trapped% = (1 - f(T)) * 100, extractable% = max(0, SOC% - trapped%)
      if (socStats->temp_derating_factor < 0.999f && socStats->temp_derating_factor > 0.0f) {
        float trapped_pct = (1.0f - socStats->temp_derating_factor) * 100.0f;
        float derated_soc = soc - trapped_pct;
        if (derated_soc < 0.0f) derated_soc = 0.0f;
        if (derated_soc > 100.0f) derated_soc = 100.0f;
        snprintf(reply, maxlen, "B:%.2fV/%s/%s SOC:%.1f%% (%.0f%%) S:%.2fV/%s",
                 telemetry->battery.voltage / 1000.0f, bat_current_str, temp_str,
                 soc, derated_soc, telemetry->solar.voltage / 1000.0f, sol_current_str);
      } else {
        snprintf(reply, maxlen, "B:%.2fV/%s/%s SOC:%.1f%% S:%.2fV/%s",
                 telemetry->battery.voltage / 1000.0f, bat_current_str, temp_str,
                 soc, telemetry->solar.voltage / 1000.0f, sol_current_str);
      }
    } else {
      snprintf(reply, maxlen, "B:%.2fV/%s/%s SOC:N/A S:%.2fV/%s",
               telemetry->battery.voltage / 1000.0f, bat_current_str, temp_str,
               telemetry->solar.voltage / 1000.0f, sol_current_str);
    }
    return true;
  } else if (strcmp(cmd, "conf") == 0) {
    const char* batType = BoardConfigContainer::getBatteryTypeCommandString(cfg.getBatteryType());
    const auto* confProps = BoardConfigContainer::getBatteryProperties(cfg.getBatteryType());
    const char* frostBehaviour = (confProps && confProps->ts_ignore)
        ? "N/A"
        : BoardConfigContainer::getFrostChargeBehaviourCommandString(cfg.getFrostChargeBehaviour());

    if (cfg.getBatteryType() == BoardConfigContainer::BAT_UNKNOWN) {
      snprintf(reply, maxlen, "B:%s (no battery, charging disabled)", batType);
    } else {
      float chargeVoltage = cfg.getMaxChargeVoltage();
      float voltage0Soc =
          BoardConfigContainer::getLowVoltageWakeThreshold(cfg.getBatteryType()) / 1000.0f;
      const char* imax = cfg.getChargeCurrentAsStr();
      bool mpptEnabled = cfg.getMPPTEnabled();
      snprintf(reply, maxlen, "B:%s F:%s M:%s I:%s Vco:%.2f V0:%.2f", batType, frostBehaviour,
               mpptEnabled ? "1" : "0", imax, chargeVoltage, voltage0Soc);
    }
    return true;
  } else if (strcmp(cmd, "tccal") == 0) {
    snprintf(reply, maxlen, "TC offset: %+.2f C (0.00=default)", cfg.getTcCalOffset());
    return true;
  } else if (strcmp(cmd, "leds") == 0) {
    snprintf(reply, maxlen, "LEDs: %s (Heartbeat + BQ Stat)",
             cfg.getLEDsEnabled() ? "ON" : "OFF");
    return true;
  } else if (strcmp(cmd, "batcap") == 0) {
    float capacity_mah = cfg.getBatteryCapacity();
    bool explicitly_set = cfg.isBatteryCapacitySet();
    snprintf(reply, maxlen, "%.0f mAh (%s)", capacity_mah, explicitly_set ? "set" : "default");
    return true;
  }

  snprintf(reply, maxlen,
           "Err: bat|fmax|imax|mppt|telem|stats|cinfo|conf|tccal|leds|batcap");
  return true;
}

const char* handleSet(BoardConfigContainer& cfg, const char* setCommand) {
  static char ret[100];
  memset(ret, 0, sizeof(ret));

  if (strncmp(setCommand, "bat ", 4) == 0) {
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[4]));
    BoardConfigContainer::BatteryType bt = BoardConfigContainer::getBatteryTypeFromCommandString(value);
    if (bt != BoardConfigContainer::BatteryType::BAT_UNKNOWN || strcmp(value, "none") == 0) {
      cfg.setBatteryType(bt);
      snprintf(ret, sizeof(ret), "Bat set to %s",
               BoardConfigContainer::getBatteryTypeCommandString(cfg.getBatteryType()));
    } else {
      snprintf(ret, sizeof(ret), "Err: Try one of: %s",
               BoardConfigContainer::getAvailableBatOptions());
    }
    return ret;
  } else if (strncmp(setCommand, "fmax ", 5) == 0) {
    const auto* fmaxProps = BoardConfigContainer::getBatteryProperties(cfg.getBatteryType());
    if (fmaxProps && fmaxProps->ts_ignore) {
      snprintf(ret, sizeof(ret), "Err: Fmax setting N/A for this chemistry (JEITA disabled)");
      return ret;
    }
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[5]));
    BoardConfigContainer::FrostChargeBehaviour fcb =
        BoardConfigContainer::getFrostChargeBehaviourFromCommandString(value);
    if (fcb != BoardConfigContainer::FrostChargeBehaviour::REDUCE_UNKNOWN) {
      cfg.setFrostChargeBehaviour(fcb);
      snprintf(ret, sizeof(ret), "Fmax charge current set to %s of imax",
               BoardConfigContainer::getFrostChargeBehaviourCommandString(cfg.getFrostChargeBehaviour()));
    } else {
      snprintf(ret, sizeof(ret), "Err: Try one of: %s",
               BoardConfigContainer::getAvailableFrostChargeBehaviourOptions());
    }
    return ret;
  } else if (strncmp(setCommand, "imax ", 5) == 0) {
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[5]));
    int ma = atoi(value);
    if (ma >= 50 && ma <= 1500) {
      cfg.setMaxChargeCurrent_mA(ma);
      snprintf(ret, sizeof(ret), "Max charge current set to %s", cfg.getChargeCurrentAsStr());
      return ret;
    }
    return "Err: Try 50-1500";
  } else if (strncmp(setCommand, "mppt ", 5) == 0) {
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[5]));
    char lowerValue[20];
    strncpy(lowerValue, value, sizeof(lowerValue) - 1);
    lowerValue[sizeof(lowerValue) - 1] = '\0';
    for (char* p = lowerValue; *p; ++p) *p = tolower(*p);

    if (strcmp(lowerValue, "true") == 0 || strcmp(lowerValue, "1") == 0) {
      cfg.setMPPTEnable(true);
      snprintf(ret, sizeof(ret), "MPPT enabled");
      return ret;
    } else if (strcmp(lowerValue, "false") == 0 || strcmp(lowerValue, "0") == 0) {
      cfg.setMPPTEnable(false);
      snprintf(ret, sizeof(ret), "MPPT disabled");
      return ret;
    }
    return "Err: Try true|false or 1|0";
  } else if (strncmp(setCommand, "batcap ", 7) == 0) {
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[7]));
    float capacity_mah = atof(value);
    if (cfg.setBatteryCapacity(capacity_mah)) {
      snprintf(ret, sizeof(ret), "Battery capacity set to %.0f mAh", capacity_mah);
    } else {
      snprintf(ret, sizeof(ret), "Err: Invalid capacity (100-100000 mAh)");
    }
    return ret;
  } else if (strncmp(setCommand, "tccal", 5) == 0) {
    // `set board.tccal`        -> auto-read BME280 as reference
    // `set board.tccal reset`  -> reset to 0.00
    const char* rest = &setCommand[5];
    if (*rest == ' ') rest++;
    const char* value = BoardConfigContainer::trim(const_cast<char*>(rest));

    if (strcmp(value, "reset") == 0 || strcmp(value, "RESET") == 0) {
      if (cfg.setTcCalOffset(0.0f)) {
        snprintf(ret, sizeof(ret), "TC calibration reset to 0.00 (default)");
      } else {
        snprintf(ret, sizeof(ret), "Err: Failed to reset TC calibration");
      }
      return ret;
    }

    float bme_avg = 0.0f;
    float new_offset = cfg.performTcCalibration(&bme_avg);
    if (new_offset > -900.0f) {
      snprintf(ret, sizeof(ret), "TC auto-cal: BME=%.1f offset=%+.2f C", bme_avg, new_offset);
    } else {
      snprintf(ret, sizeof(ret), "Err: Auto-cal failed (BME280/NTC error?)");
    }
    return ret;
  } else if (strncmp(setCommand, "leds ", 5) == 0) {
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[5]));
    bool enabled  = (strcmp(value, "1") == 0 || strcmp(value, "on") == 0 || strcmp(value, "ON") == 0);
    bool disabled = (strcmp(value, "0") == 0 || strcmp(value, "off") == 0 || strcmp(value, "OFF") == 0);
    if (enabled || disabled) {
      cfg.setLEDsEnabled(enabled);
      snprintf(ret, sizeof(ret), "LEDs %s (Heartbeat + BQ Stat)",
               enabled ? "enabled" : "disabled");
    } else {
      snprintf(ret, sizeof(ret), "Err: Use 'on/1' or 'off/0'");
    }
    return ret;
  } else if (strncmp(setCommand, "soc ", 4) == 0) {
    const char* value = BoardConfigContainer::trim(const_cast<char*>(&setCommand[4]));
    float soc_percent = atof(value);
    if (BoardConfigContainer::setSOCManually(soc_percent)) {
      snprintf(ret, sizeof(ret), "SOC set to %.1f%%", soc_percent);
    } else {
      snprintf(ret, sizeof(ret), "Err: Invalid SOC (0-100) or INA228 not ready");
    }
    return ret;
  }

  snprintf(ret, sizeof(ret), "Err: bat|imax|fmax|mppt|batcap|tccal|leds|soc");
  return ret;
}

} // namespace inhero
