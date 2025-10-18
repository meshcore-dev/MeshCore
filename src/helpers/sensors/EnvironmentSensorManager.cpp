#include "EnvironmentSensorManager.h"
#include <target.h>

#if ENV_PIN_SDA && ENV_PIN_SCL
#define TELEM_WIRE &Wire1  // Use Wire1 as the I2C bus for Environment Sensors
#else
#define TELEM_WIRE &Wire  // Use default I2C bus for Environment Sensors
#endif

#ifdef ENV_INCLUDE_BME680
#ifndef TELEM_BME680_ADDRESS
#define TELEM_BME680_ADDRESS 0x76
#endif
#define TELEM_BME680_SEALEVELPRESSURE_HPA (1013.25)
#include <Adafruit_BME680.h>
static Adafruit_BME680 BME680;
#endif

#if ENV_INCLUDE_AHTX0
#define TELEM_AHTX_ADDRESS      0x38      // AHT10, AHT20 temperature and humidity sensor I2C address
#include <Adafruit_AHTX0.h>
static Adafruit_AHTX0 AHTX0;
#endif

#if ENV_INCLUDE_BME280
#ifndef TELEM_BME280_ADDRESS
#define TELEM_BME280_ADDRESS    0x76      // BME280 environmental sensor I2C address
#endif
#define TELEM_BME280_SEALEVELPRESSURE_HPA (1013.25)    // Athmospheric pressure at sea level
#include <Adafruit_BME280.h>
static Adafruit_BME280 BME280;
#endif

#if ENV_INCLUDE_BMP280
#ifndef TELEM_BMP280_ADDRESS
#define TELEM_BMP280_ADDRESS    0x76      // BMP280 environmental sensor I2C address
#endif
#define TELEM_BMP280_SEALEVELPRESSURE_HPA (1013.25)    // Athmospheric pressure at sea level
#include <Adafruit_BMP280.h>
static Adafruit_BMP280 BMP280;
#endif

#if ENV_INCLUDE_SHTC3
#include <Adafruit_SHTC3.h>
static Adafruit_SHTC3 SHTC3;
#endif

#if ENV_INCLUDE_SHT4X
#define TELEM_SHT4X_ADDRESS 0x44  //0x44 - 0x46
#include <SensirionI2cSht4x.h>
static SensirionI2cSht4x SHT4X;
#endif

#if ENV_INCLUDE_LPS22HB
#include <Arduino_LPS22HB.h>
#endif

#if ENV_INCLUDE_INA3221
#define TELEM_INA3221_ADDRESS   0x42      // INA3221 3 channel current sensor I2C address
#define TELEM_INA3221_SHUNT_VALUE 0.100 // most variants will have a 0.1 ohm shunts
#define TELEM_INA3221_NUM_CHANNELS 3
#include <Adafruit_INA3221.h>
static Adafruit_INA3221 INA3221;
#endif

#if ENV_INCLUDE_INA219
#define TELEM_INA219_ADDRESS    0x40      // INA219 single channel current sensor I2C address
#include <Adafruit_INA219.h>
static Adafruit_INA219 INA219(TELEM_INA219_ADDRESS);
#endif

#if ENV_INCLUDE_INA260
#define TELEM_INA260_ADDRESS    0x41      // INA260 single channel current sensor I2C address
#include <Adafruit_INA260.h>
static Adafruit_INA260 INA260;
#endif

#if ENV_INCLUDE_INA226
#define TELEM_INA226_ADDRESS    0x44
#define TELEM_INA226_SHUNT_VALUE 0.100
#define TELEM_INA226_MAX_AMP 0.8
#include <INA226.h>
static INA226 INA226(TELEM_INA226_ADDRESS, TELEM_WIRE);
#endif

#if ENV_INCLUDE_MLX90614
#define TELEM_MLX90614_ADDRESS 0x5A      // MLX90614 IR temperature sensor I2C address
#include <Adafruit_MLX90614.h>
static Adafruit_MLX90614 MLX90614;
#endif

#if ENV_INCLUDE_VL53L0X
#define TELEM_VL53L0X_ADDRESS 0x29      // VL53L0X time-of-flight distance sensor I2C address
#include <Adafruit_VL53L0X.h>
static Adafruit_VL53L0X VL53L0X;
#endif


bool EnvironmentSensorManager::begin() {
  #if ENV_INCLUDE_GPS
  GpsDriver* driver = LocationProvider::detectDriver();
  _gps = new LocationProvider(driver, &rtc_clock);
  _gps->begin();
  #endif

  #if ENV_PIN_SDA && ENV_PIN_SCL
    #ifdef NRF52_PLATFORM
  Wire1.setPins(ENV_PIN_SDA, ENV_PIN_SCL);
  Wire1.setClock(100000);
  Wire1.begin();
    #else
  Wire1.begin(ENV_PIN_SDA, ENV_PIN_SCL, 100000);
    #endif
  MESH_DEBUG_PRINTLN("Second I2C initialized on pins SDA: %d SCL: %d", ENV_PIN_SDA, ENV_PIN_SCL);
  #endif

  #if ENV_INCLUDE_AHTX0
  if (AHTX0.begin(TELEM_WIRE, 0, TELEM_AHTX_ADDRESS)) {
    MESH_DEBUG_PRINTLN("Found AHT10/AHT20 at address: %02X", TELEM_AHTX_ADDRESS);
    AHTX0_initialized = true;
  } else {
    AHTX0_initialized = false;
    MESH_DEBUG_PRINTLN("AHT10/AHT20 was not found at I2C address %02X", TELEM_AHTX_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_BME280
  if (BME280.begin(TELEM_BME280_ADDRESS, TELEM_WIRE)) {
    MESH_DEBUG_PRINTLN("Found BME280 at address: %02X", TELEM_BME280_ADDRESS);
    MESH_DEBUG_PRINTLN("BME sensor ID: %02X", BME280.sensorID());
    BME280_initialized = true;
  } else {
    BME280_initialized = false;
    MESH_DEBUG_PRINTLN("BME280 was not found at I2C address %02X", TELEM_BME280_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_BMP280
  if (BMP280.begin(TELEM_BMP280_ADDRESS)) {
    MESH_DEBUG_PRINTLN("Found BMP280 at address: %02X", TELEM_BMP280_ADDRESS);
    MESH_DEBUG_PRINTLN("BMP sensor ID: %02X", BMP280.sensorID());
    BMP280_initialized = true;
  } else {
    BMP280_initialized = false;
    MESH_DEBUG_PRINTLN("BMP280 was not found at I2C address %02X", TELEM_BMP280_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_SHTC3
  if (SHTC3.begin()) {
    MESH_DEBUG_PRINTLN("Found sensor: SHTC3");
    SHTC3_initialized = true;
  } else {
    SHTC3_initialized = false;
    MESH_DEBUG_PRINTLN("SHTC3 was not found at I2C address %02X", 0x70);
  }
  #endif


  #if ENV_INCLUDE_SHT4X
  SHT4X.begin(*TELEM_WIRE, TELEM_SHT4X_ADDRESS);
  uint32_t serialNumber = 0;
  int16_t sht4x_error;
  sht4x_error = SHT4X.serialNumber(serialNumber);
  if (sht4x_error == 0) {
    MESH_DEBUG_PRINTLN("Found SHT4X at address: %02X", TELEM_SHT4X_ADDRESS);
    SHT4X_initialized = true;
  } else {
    SHT4X_initialized = false;
    MESH_DEBUG_PRINTLN("SHT4X was not found at I2C address %02X", TELEM_SHT4X_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_LPS22HB
  if (BARO.begin()) {
    MESH_DEBUG_PRINTLN("Found sensor: LPS22HB");
    LPS22HB_initialized = true;
  } else {
    LPS22HB_initialized = false;
    MESH_DEBUG_PRINTLN("LPS22HB was not found at I2C address %02X", 0x5C);
  }
  #endif

  #if ENV_INCLUDE_INA3221
  if (INA3221.begin(TELEM_INA3221_ADDRESS, TELEM_WIRE)) {
    MESH_DEBUG_PRINTLN("Found INA3221 at address: %02X", TELEM_INA3221_ADDRESS);
    MESH_DEBUG_PRINTLN("%04X %04X", INA3221.getDieID(), INA3221.getManufacturerID());

    for(int i = 0; i < 3; i++) {
      INA3221.setShuntResistance(i, TELEM_INA3221_SHUNT_VALUE);
    }
    INA3221_initialized = true;
  } else {
    INA3221_initialized = false;
    MESH_DEBUG_PRINTLN("INA3221 was not found at I2C address %02X", TELEM_INA3221_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_INA219
  if (INA219.begin(TELEM_WIRE)) {
    MESH_DEBUG_PRINTLN("Found INA219 at address: %02X", TELEM_INA219_ADDRESS);
    INA219_initialized = true;
  } else {
    INA219_initialized = false;
    MESH_DEBUG_PRINTLN("INA219 was not found at I2C address %02X", TELEM_INA219_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_INA260
  if (INA260.begin(TELEM_INA260_ADDRESS, TELEM_WIRE)) {
    MESH_DEBUG_PRINTLN("Found INA260 at address: %02X", TELEM_INA260_ADDRESS);
    INA260_initialized = true;
  } else {
    INA260_initialized = false;
    MESH_DEBUG_PRINTLN("INA260 was not found at I2C address %02X", TELEM_INA219_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_INA226
  if (INA226.begin()) {
    MESH_DEBUG_PRINTLN("Found INA226 at address: %02X", TELEM_INA226_ADDRESS);
    INA226.setMaxCurrentShunt(TELEM_INA226_MAX_AMP, TELEM_INA226_SHUNT_VALUE);
    INA226_initialized = true;
  } else {
    INA226_initialized = false;
    MESH_DEBUG_PRINTLN("INA226 was not found at I2C address %02X", TELEM_INA226_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_MLX90614
  if (MLX90614.begin(TELEM_MLX90614_ADDRESS, TELEM_WIRE)) {
    MESH_DEBUG_PRINTLN("Found MLX90614 at address: %02X", TELEM_MLX90614_ADDRESS);
    MLX90614_initialized = true;
  } else {
    MLX90614_initialized = false;
    MESH_DEBUG_PRINTLN("MLX90614 was not found at I2C address %02X", TELEM_MLX90614_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_VL53L0X
  if (VL53L0X.begin(TELEM_VL53L0X_ADDRESS, false, TELEM_WIRE)) {
    MESH_DEBUG_PRINTLN("Found VL53L0X at address: %02X", TELEM_VL53L0X_ADDRESS);
    VL53L0X_initialized = true;
  } else {
    VL53L0X_initialized = false;
    MESH_DEBUG_PRINTLN("VL53L0X was not found at I2C address %02X", TELEM_VL53L0X_ADDRESS);
  }
  #endif

  #if ENV_INCLUDE_BME680
  if (BME680.begin(TELEM_BME680_ADDRESS, TELEM_WIRE)) {
    MESH_DEBUG_PRINTLN("Found BME680 at address: %02X", TELEM_BME680_ADDRESS);
    BME680_initialized = true;
  } else {
    BME680_initialized = false;
    MESH_DEBUG_PRINTLN("BME680 was not found at I2C address %02X", TELEM_BME680_ADDRESS);
  }
  #endif

  return true;
}

bool EnvironmentSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  next_available_channel = TELEM_CHANNEL_SELF + 1;

  #if ENV_INCLUDE_GPS
  if (requester_permissions & TELEM_PERM_LOCATION && _gps->isDetected()) {
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  #endif

  if (requester_permissions & TELEM_PERM_ENVIRONMENT) {

    #if ENV_INCLUDE_AHTX0
    if (AHTX0_initialized) {
      sensors_event_t humidity, temp;
      AHTX0.getEvent(&humidity, &temp);
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temp.temperature);
      telemetry.addRelativeHumidity(TELEM_CHANNEL_SELF, humidity.relative_humidity);
    }
    #endif

    #if ENV_INCLUDE_BME280
    if (BME280_initialized) {
      telemetry.addTemperature(TELEM_CHANNEL_SELF, BME280.readTemperature());
      telemetry.addRelativeHumidity(TELEM_CHANNEL_SELF, BME280.readHumidity());
      telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, BME280.readPressure()/100);
      telemetry.addAltitude(TELEM_CHANNEL_SELF, BME280.readAltitude(TELEM_BME280_SEALEVELPRESSURE_HPA));
    }
    #endif

    #if ENV_INCLUDE_BMP280
    if (BMP280_initialized) {
      telemetry.addTemperature(TELEM_CHANNEL_SELF, BMP280.readTemperature());
      telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, BMP280.readPressure()/100);
      telemetry.addAltitude(TELEM_CHANNEL_SELF, BMP280.readAltitude(TELEM_BMP280_SEALEVELPRESSURE_HPA));
    }
    #endif

    #if ENV_INCLUDE_SHTC3
    if (SHTC3_initialized) {
      sensors_event_t humidity, temp;
      SHTC3.getEvent(&humidity, &temp);

      telemetry.addTemperature(TELEM_CHANNEL_SELF, temp.temperature);
      telemetry.addRelativeHumidity(TELEM_CHANNEL_SELF, humidity.relative_humidity);
    }
    #endif

    #if ENV_INCLUDE_SHT4X
    if (SHT4X_initialized) {
      float sht4x_humidity, sht4x_temperature;
      int16_t sht4x_error;
      sht4x_error = SHT4X.measureLowestPrecision(sht4x_temperature, sht4x_humidity);
      if (sht4x_error == 0) {
        telemetry.addTemperature(TELEM_CHANNEL_SELF, sht4x_temperature);
        telemetry.addRelativeHumidity(TELEM_CHANNEL_SELF, sht4x_humidity);
      }
    }
    #endif

    #if ENV_INCLUDE_LPS22HB
    if (LPS22HB_initialized) {
      telemetry.addTemperature(TELEM_CHANNEL_SELF, BARO.readTemperature());
      telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, BARO.readPressure());
    }
    #endif

    #if ENV_INCLUDE_INA3221
    if (INA3221_initialized) {
      for(int i = 0; i < TELEM_INA3221_NUM_CHANNELS; i++) {
        // add only enabled INA3221 channels to telemetry
        if (INA3221.isChannelEnabled(i)) {
          float voltage = INA3221.getBusVoltage(i);
          float current = INA3221.getCurrentAmps(i);
          telemetry.addVoltage(next_available_channel, voltage);
          telemetry.addCurrent(next_available_channel, current);
          telemetry.addPower(next_available_channel, voltage * current);
          next_available_channel++;
        }
      }
    }
    #endif

    #if ENV_INCLUDE_INA219
    if (INA219_initialized) {
      telemetry.addVoltage(next_available_channel, INA219.getBusVoltage_V());
      telemetry.addCurrent(next_available_channel, INA219.getCurrent_mA() / 1000);
      telemetry.addPower(next_available_channel, INA219.getPower_mW() / 1000);
      next_available_channel++;
    }
    #endif

    #if ENV_INCLUDE_INA260
    if (INA260_initialized) {
      telemetry.addVoltage(next_available_channel, INA260.readBusVoltage() / 1000);
      telemetry.addCurrent(next_available_channel, INA260.readCurrent() / 1000);
      telemetry.addPower(next_available_channel, INA260.readPower() / 1000);
      next_available_channel++;
    }
    #endif

    #if ENV_INCLUDE_INA226
    if (INA226_initialized) {
      telemetry.addVoltage(next_available_channel, INA226.getBusVoltage());
      telemetry.addCurrent(next_available_channel, INA226.getCurrent_mA() / 1000.0);
      telemetry.addPower(next_available_channel, INA226.getPower_mW() / 1000.0);
      next_available_channel++;
    }
    #endif

    #if ENV_INCLUDE_MLX90614
    if (MLX90614_initialized) {
      telemetry.addTemperature(TELEM_CHANNEL_SELF, MLX90614.readObjectTempC());
      telemetry.addTemperature(TELEM_CHANNEL_SELF + 1, MLX90614.readAmbientTempC());
    }
    #endif

    #if ENV_INCLUDE_VL53L0X
    if (VL53L0X_initialized) {
      VL53L0X_RangingMeasurementData_t measure;
      VL53L0X.rangingTest(&measure, false); // pass in 'true' to get debug data
      if (measure.RangeStatus != 4) { // phase failures
        telemetry.addDistance(TELEM_CHANNEL_SELF, measure.RangeMilliMeter / 1000.0f); // convert mm to m
      } else {
        telemetry.addDistance(TELEM_CHANNEL_SELF, 0.0f); // no valid measurement
      }
    }
    #endif

    #if ENV_INCLUDE_BME680
    if (BME680_initialized) {
      if (BME680.performReading()) {
        telemetry.addTemperature(TELEM_CHANNEL_SELF, BME680.temperature);
        telemetry.addRelativeHumidity(TELEM_CHANNEL_SELF, BME680.humidity);
        telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, BME680.pressure / 100);
        telemetry.addAltitude(TELEM_CHANNEL_SELF, 44330.0 * (1.0 - pow((BME680.pressure / 100) / TELEM_BME680_SEALEVELPRESSURE_HPA, 0.1903)));
        telemetry.addAnalogInput(next_available_channel, BME680.gas_resistance);
        next_available_channel++;
      }
    }
    #endif

  }

  return true;
}


int EnvironmentSensorManager::getNumSettings() const {
  int settings = 0;
  #if ENV_INCLUDE_GPS
    if (_gps->isDetected()) settings++;  // only show GPS setting if GPS is detected
  #endif
  return settings;
}

const char* EnvironmentSensorManager::getSettingName(int i) const {
  int settings = 0;
  #if ENV_INCLUDE_GPS
    if (_gps->isDetected() && i == settings++) {
      return "gps";
    }
  #endif
  return NULL;
}

const char* EnvironmentSensorManager::getSettingValue(int i) const {
  int settings = 0;
  #if ENV_INCLUDE_GPS
    if (_gps->isDetected() && i == settings++) {
      return _gps->isEnabled() ? "1" : "0";
    }
  #endif
  return NULL;
}

bool EnvironmentSensorManager::setSettingValue(const char* name, const char* value) {
  #if ENV_INCLUDE_GPS
  if (_gps->isDetected() && strcmp(name, "gps") == 0) {
    _gps->setEnabled(strcmp(value, "0") != 0);
    return true;
  }
  if (_gps->isDetected() && strcmp(name, "gps_interval") == 0) {
    _gps->setCycleInterval(atoi(value));
    return true;
  }
  #endif
  return false;  // not supported
}

#if ENV_INCLUDE_GPS
void EnvironmentSensorManager::loop() {
  _gps->loop();

  // Sync coordinates to SensorManager
  node_lat = _gps->node_lat;
  node_lon = _gps->node_lon;
  node_altitude = _gps->node_altitude;
}
#endif
