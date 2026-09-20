/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#include "InheroMr2SensorManager.h"

#include <Adafruit_BME280.h>
#include <Wire.h>
#include <math.h>

#ifndef TELEM_BME280_ADDRESS
#define TELEM_BME280_ADDRESS 0x76
#endif

namespace {
Adafruit_BME280 bme280;
float station_altitude_m = NAN;

void queryBme280(uint8_t channel, uint8_t, CayenneLPP& telemetry) {
  if (!bme280.takeForcedMeasurement()) return;

  telemetry.addTemperature(channel, bme280.readTemperature());
  telemetry.addRelativeHumidity(channel, bme280.readHumidity());
  const float pressure_hpa = bme280.readPressure() / 100.0f;
  if (isnan(station_altitude_m)) {
    telemetry.addBarometricPressure(channel, pressure_hpa);
    telemetry.addAltitude(channel, bme280.readAltitude(1013.25f));
  } else {
    telemetry.addBarometricPressure(
        channel, InheroMr2SensorManager::pressureToQnh(pressure_hpa, station_altitude_m));
    telemetry.addAltitude(channel, station_altitude_m);
  }
}
}

bool InheroMr2SensorManager::begin() {
  // The base resets the registry and retains optional GPS and other sensors.
  if (!EnvironmentSensorManager::begin()) return false;
  if (_active_sensor_count >= MAX_ACTIVE_SENSORS) return true;

#if ENV_PIN_SDA && ENV_PIN_SCL
  TwoWire* wire = &Wire1;
#else
  TwoWire* wire = &Wire;
#endif
  const uint8_t addresses[] = {
    TELEM_BME280_ADDRESS, TELEM_BME280_ADDRESS == 0x77 ? 0x76 : 0x77
  };
  for (uint8_t address : addresses) {
    wire->beginTransmission(address);
    if (wire->endTransmission() != 0) continue;
    if (!bme280.begin(address, wire)) continue;

    bme280.setSampling(Adafruit_BME280::MODE_FORCED,
                       Adafruit_BME280::SAMPLING_X1,
                       Adafruit_BME280::SAMPLING_X1,
                       Adafruit_BME280::SAMPLING_X1,
                       Adafruit_BME280::FILTER_OFF,
                       Adafruit_BME280::STANDBY_MS_1000);
    _active_sensors[_active_sensor_count++] = { queryBme280, 0 };
    MESH_DEBUG_PRINTLN("Found MR2 BME280 at address: %02X", address);
    break;  // One physical BME280; the second address is only a fallback.
  }
  return true;
}

void InheroMr2SensorManager::setBme280StationAltitude(float altitude_m) {
  station_altitude_m = altitude_m;
}

float InheroMr2SensorManager::getBme280StationAltitude() {
  return station_altitude_m;
}

float InheroMr2SensorManager::pressureToQnh(float pressure_hpa, float altitude_m) {
  // ICAO standard atmosphere, matching Adafruit's seaLevelForAltitude().
  return pressure_hpa / powf(1.0f - altitude_m / 44330.0f, 5.255f);
}
