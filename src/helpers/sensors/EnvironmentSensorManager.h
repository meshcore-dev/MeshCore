#pragma once
#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

// Forward declaration for board-specific power management
class HeltecV3Board;

class EnvironmentSensorManager : public SensorManager {
protected:
  int next_available_channel = TELEM_CHANNEL_SELF + 1;
  bool AHTX0_initialized = false;
  bool BME280_initialized = false;
  bool BMP280_initialized = false;
  bool BME680_initialized = false;
  bool INA3221_initialized = false;
  bool INA219_initialized = false;
  bool INA260_initialized = false;
  bool INA226_initialized = false;
  bool SHTC3_initialized = false;
  bool LPS22HB_initialized = false;
  bool MLX90614_initialized = false;
  bool VL53L0X_initialized = false;
  bool SHT4X_initialized = false;
  bool BMP085_initialized = false;
  bool gps_detected = false;
  bool gps_active = false;

#if ENV_INCLUDE_BME680
  unsigned long last_bme680_reading = 0;
  float bme680_temperature = 0;
  float bme680_humidity = 0;
  float bme680_pressure = 0;
  float bme680_gas_resistance = 0;
  float bme680_altitude = 0;
  float bme680_iaq = 0;          // Indoor Air Quality index (only with BSEC2)
  float bme680_iaq_accuracy = 0; // Indoor Air Quality accuracy (only with BSEC2)

#endif

#if ENV_INCLUDE_GPS
  LocationProvider *_location;
  HeltecV3Board *_board;           // Optional board reference for power management
  bool gps_needs_power_on = false; // Flag for deferred power-on in PERSISTANT_GPS mode
  void start_gps();
  void stop_gps();
  void initBasicGPS();
#ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
#endif
#endif

public:
#if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location, HeltecV3Board *board = nullptr)
      : _location(&location), _board(board) {};
  LocationProvider *getLocationProvider() { return _location; }
#else
  EnvironmentSensorManager() {};
#endif

  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP &telemetry) override;

#if ENV_INCLUDE_GPS || ENV_INCLUDE_BME680
  void loop() override;
#endif

  int getNumSettings() const override;
  const char *getSettingName(int i) const override;
  const char *getSettingValue(int i) const override;
  bool setSettingValue(const char *name, const char *value) override;
};