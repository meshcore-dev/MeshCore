#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>

class EnvironmentSensorManager : public SensorManager {
protected:
  int next_available_channel = TELEM_CHANNEL_SELF + 1;

  bool AHTX0_initialized = false;
  bool BME280_initialized = false;
  bool BMP280_initialized = false;
  bool INA3221_initialized = false;
  bool INA219_initialized = false;
  bool INA260_initialized = false;
  bool INA226_initialized = false;
  bool SHTC3_initialized = false;
  bool LPS22HB_initialized = false;
  bool MLX90614_initialized = false;
  bool VL53L0X_initialized = false;
  bool SHT4X_initialized = false;
  bool BME680_initialized = false;
  bool BMP085_initialized = false;

public:
  EnvironmentSensorManager(){};
#if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider& loc) { registerLocationProvider(&loc); }
#endif
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
};
