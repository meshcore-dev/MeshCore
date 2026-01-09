#include "EnvironmentSensorManager.h"

// Include board headers for power management
#ifdef HELTEC_LORA_V3
#include <helpers/RefCountedDigitalPin.h>
// Forward declare to avoid circular dependency
#ifndef HELTEC_V3_BOARD_CLASS_DEFINED
class HeltecV3Board {
public:
  RefCountedDigitalPin periph_power;
  HeltecV3Board() : periph_power(PIN_VEXT_EN) {}
};
#endif
#endif

#if ENV_PIN_SDA && ENV_PIN_SCL
#define TELEM_WIRE &Wire1 // Use Wire1 as the I2C bus for Environment Sensors
#else
#define TELEM_WIRE &Wire // Use default I2C bus for Environment Sensors
#endif

#if ENV_INCLUDE_BME680
#ifndef TELEM_BME680_ADDRESS
#define TELEM_BME680_ADDRESS 0x76
#endif
#define TELEM_BME680_SEALEVELPRESSURE_HPA (1013.25)

#ifndef BME680_BSEC2_SUPPORTED
#define BME680_BSEC2_SUPPORTED 0
#endif

// Geoidal separation (geoid height above WGS84 ellipsoid) for altitude correction
// This is location-specific. Default is 0, can be overridden in platformio.ini
// For Netherlands: typically +45 to +48 meters
// Find your value at: https://geographiclib.sourceforge.io/cgi-bin/GeoidEval
#ifndef GEOIDAL_SEPARATION
#define GEOIDAL_SEPARATION 0.0
#endif

#ifdef BME680_BSEC2_SUPPORTED
// Use BSEC2 library for IAQ support
#include <bsec2.h>
static Bsec2 BME680_BSEC;

// BSEC configuration for 3 second sample rate
const uint8_t bsec_config[] = {
#include "config/bme680/bme680_iaq_33v_3s_4d/bsec_iaq.txt"
};

#else
// Use Adafruit BME680 library (no IAQ)
#include <Adafruit_BME680.h>
#if ENV_PIN_SDA && ENV_PIN_SCL
static Adafruit_BME680 BME680(&Wire1); // Use Wire1 for sensors
#else
static Adafruit_BME680 BME680(&Wire); // Use default Wire
#endif
#endif
#endif

#ifdef ENV_INCLUDE_BMP085
#define TELEM_BMP085_SEALEVELPRESSURE_HPA (1013.25)
#include <Adafruit_BMP085.h>
static Adafruit_BMP085 BMP085;
#endif

#if ENV_INCLUDE_AHTX0
#define TELEM_AHTX_ADDRESS 0x38 // AHT10, AHT20 temperature and humidity sensor I2C address
#include <Adafruit_AHTX0.h>
static Adafruit_AHTX0 AHTX0;
#endif

#if ENV_INCLUDE_BME280
#ifndef TELEM_BME280_ADDRESS
#define TELEM_BME280_ADDRESS 0x76 // BME280 environmental sensor I2C address
#endif
#define TELEM_BME280_SEALEVELPRESSURE_HPA (1013.25) // Athmospheric pressure at sea level
#include <Adafruit_BME280.h>
static Adafruit_BME280 BME280;
#endif

#if ENV_INCLUDE_BMP280
#ifndef TELEM_BMP280_ADDRESS
#define TELEM_BMP280_ADDRESS 0x76 // BMP280 environmental sensor I2C address
#endif
#define TELEM_BMP280_SEALEVELPRESSURE_HPA (1013.25) // Athmospheric pressure at sea level
#include <Adafruit_BMP280.h>
static Adafruit_BMP280 BMP280;
#endif

#if ENV_INCLUDE_SHTC3
#include <Adafruit_SHTC3.h>
static Adafruit_SHTC3 SHTC3;
#endif

#if ENV_INCLUDE_SHT4X
#define TELEM_SHT4X_ADDRESS 0x44 // 0x44 - 0x46
#include <SensirionI2cSht4x.h>
static SensirionI2cSht4x SHT4X;
#endif

#if ENV_INCLUDE_LPS22HB
#include <Arduino_LPS22HB.h>
#endif

#if ENV_INCLUDE_INA3221
#define TELEM_INA3221_ADDRESS      0x42  // INA3221 3 channel current sensor I2C address
#define TELEM_INA3221_SHUNT_VALUE  0.100 // most variants will have a 0.1 ohm shunts
#define TELEM_INA3221_NUM_CHANNELS 3
#include <Adafruit_INA3221.h>
static Adafruit_INA3221 INA3221;
#endif

#if ENV_INCLUDE_INA219
#define TELEM_INA219_ADDRESS 0x40 // INA219 single channel current sensor I2C address
#include <Adafruit_INA219.h>
static Adafruit_INA219 INA219(TELEM_INA219_ADDRESS);
#endif

#if ENV_INCLUDE_INA260
#define TELEM_INA260_ADDRESS 0x41 // INA260 single channel current sensor I2C address
#include <Adafruit_INA260.h>
static Adafruit_INA260 INA260;
#endif

#if ENV_INCLUDE_INA226
#define TELEM_INA226_ADDRESS     0x44
#define TELEM_INA226_SHUNT_VALUE 0.100
#define TELEM_INA226_MAX_AMP     0.8
#include <INA226.h>
static INA226 INA226_sensor(TELEM_INA226_ADDRESS, TELEM_WIRE);
#endif

#if ENV_INCLUDE_MLX90614
#define TELEM_MLX90614_ADDRESS 0x5A // MLX90614 IR temperature sensor I2C address
#include <Adafruit_MLX90614.h>
static Adafruit_MLX90614 MLX90614;
#endif

#if ENV_INCLUDE_VL53L0X
#define TELEM_VL53L0X_ADDRESS 0x29 // VL53L0X time-of-flight distance sensor I2C address
#include <Adafruit_VL53L0X.h>
static Adafruit_VL53L0X VL53L0X;
#endif

#if ENV_INCLUDE_GPS && defined(RAK_BOARD) && !defined(RAK_WISMESH_TAG)
#define RAK_WISBLOCK_GPS
#endif

#ifdef RAK_WISBLOCK_GPS
static uint32_t gpsResetPin = 0;
static bool i2cGPSFlag = false;
static bool serialGPSFlag = false;
#define TELEM_RAK12500_ADDRESS 0x42 // RAK12500 Ublox GPS via i2c
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
static SFE_UBLOX_GNSS ublox_GNSS;

class RAK12500LocationProvider : public LocationProvider {
  long _lat = 0;
  long _lng = 0;
  long _alt = 0;
  int _sats = 0;
  long _epoch = 0;
  bool _fix = false;

public:
  long getLatitude() override { return _lat; }
  long getLongitude() override { return _lng; }
  long getAltitude() override { return _alt; }
  long satellitesCount() override { return _sats; }
  bool isValid() override { return _fix; }
  long getTimestamp() override { return _epoch; }
  void sendSentence(const char *sentence) override {}
  void reset() override {}
  void begin() override {}
  void stop() override {}
  void loop() override {
    if (ublox_GNSS.getGnssFixOk(8)) {
      _fix = true;
      _lat = ublox_GNSS.getLatitude(2) / 10;
      _lng = ublox_GNSS.getLongitude(2) / 10;
      _alt = ublox_GNSS.getAltitude(2);
      _sats = ublox_GNSS.getSIV(2);
    } else {
      _fix = false;
    }
    _epoch = ublox_GNSS.getUnixEpoch(2);
  }
  bool isEnabled() override { return true; }
};

static RAK12500LocationProvider RAK12500_provider;
#endif

// I2C device scanner - checks if a device responds at the given address
static bool i2c_device_exists(TwoWire *wire, uint8_t address) {
  wire->beginTransmission(address);
  uint8_t error = wire->endTransmission();
  delay(20);
  return (error == 0);
}

// Read chip ID from Bosch sensors (BME/BMP family) - register 0xD0
static uint8_t read_chip_id(TwoWire *wire, uint8_t address) {
  wire->beginTransmission(address);
  wire->write(0xD0); // Chip ID register for Bosch sensors
  if (wire->endTransmission() != 0) {
    delay(20);
    return 0x00; // Communication failed
  }

  delay(10);
  wire->requestFrom(address, (uint8_t)1);
  if (wire->available()) {
    uint8_t chip_id = wire->read();
    delay(20);
    return chip_id;
  }
  delay(20);
  return 0x00;
}

// Get geoidal separation based on GPS coordinates
// Simple lookup table for common regions - accuracy ~5m
static float get_geoidal_separation(double lat, double lon) {
  // Coarse regional estimates (latitude/longitude-based approximation)
  // For more accuracy, use manual override with exact value for your location

  MESH_DEBUG_PRINTLN("Calculating geoidal separation for lat: %.6f, lon: %.6f", lat, lon);

  // North America
  if (lat >= 25 && lat <= 70 && lon >= -170 && lon <= -50) {
    if (lat >= 50) return -10.0; // Northern Canada/Alaska
    if (lat >= 35) return -25.0; // Northern US
    return -15.0;                // Southern US/Mexico
  }

  // Europe
  if (lat >= 35 && lat <= 72 && lon >= -10 && lon <= 45) {
    if (lat >= 60) return 25.0; // Scandinavia
    if (lat >= 50) return 45.0; // Netherlands, Germany, Poland
    if (lat >= 45) return 48.0; // Central Europe
    return 50.0;                // Southern Europe
  }

  // Asia
  if (lat >= -10 && lat <= 75 && lon >= 45 && lon <= 180) {
    if (lat >= 40) return -10.0; // Northern Asia/Russia
    if (lat >= 20) return -5.0;  // Eastern Asia
    return 0.0;                  // Southeast Asia
  }

  // Australia/Oceania
  if (lat >= -50 && lat <= -10 && lon >= 110 && lon <= 180) {
    return 10.0;
  }

  // South America
  if (lat >= -60 && lat <= 15 && lon >= -85 && lon <= -30) {
    if (lat >= 0) return 5.0;    // Northern South America
    if (lat >= -30) return 15.0; // Central South America
    return 0.0;                  // Southern South America
  }

  // Africa
  if (lat >= -35 && lat <= 38 && lon >= -20 && lon <= 55) {
    if (lat >= 20) return 0.0; // Northern Africa
    if (lat >= 0) return 5.0;  // Equatorial Africa
    return 20.0;               // Southern Africa
  }

  // Default fallback
  return 0.0;
}

bool EnvironmentSensorManager::begin() {
#if ENV_INCLUDE_GPS
#ifdef RAK_WISBLOCK_GPS
  rakGPSInit(); // probe base board/sockets for GPS
#else
  initBasicGPS();
#endif
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

#if ENV_INCLUDE_BME680
  // Check chip ID to verify it's actually a BME680 (ID=0x61) not BME280 or BMP280
  if (i2c_device_exists(TELEM_WIRE, TELEM_BME680_ADDRESS)) {
    uint8_t chip_id = read_chip_id(TELEM_WIRE, TELEM_BME680_ADDRESS);
    MESH_DEBUG_PRINTLN("Chip ID at address %02X: 0x%02X", TELEM_BME680_ADDRESS, chip_id);

    if (chip_id == 0x61) { // BME680 chip ID
      MESH_DEBUG_PRINTLN("Confirmed BME680 chip, initializing...");
      delay(100);

#ifdef BME680_BSEC2_SUPPORTED
      // Initialize with BSEC2 library for IAQ support
      if (BME680_BSEC.begin(TELEM_BME680_ADDRESS, *TELEM_WIRE)) {
        if (BME680_BSEC.status == BSEC_OK) {
          MESH_DEBUG_PRINTLN("BSEC2 sensor communication OK");

          // Set custom BSEC configuration
          BME680_BSEC.setConfig(bsec_config);

          // Define sensor outputs to subscribe to
          bsecSensor sensorList[] = { BSEC_OUTPUT_IAQ,
                                      BSEC_OUTPUT_RAW_TEMPERATURE,
                                      BSEC_OUTPUT_RAW_PRESSURE,
                                      BSEC_OUTPUT_RAW_HUMIDITY,
                                      BSEC_OUTPUT_RAW_GAS,
                                      BSEC_OUTPUT_STABILIZATION_STATUS,
                                      BSEC_OUTPUT_RUN_IN_STATUS,
                                      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
                                      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY };

          // Try LP (Low Power) mode first
          if (!BME680_BSEC.updateSubscription(sensorList, 9, BSEC_SAMPLE_RATE_LP)) {
            MESH_DEBUG_PRINTLN("BSEC LP mode failed, trying ULP mode...");
            // Fall back to ULP (Ultra Low Power) mode
            if (!BME680_BSEC.updateSubscription(sensorList, 9, BSEC_SAMPLE_RATE_ULP)) {
              MESH_DEBUG_PRINTLN("BSEC ULP mode also failed!");
              BME680_initialized = false;
            } else {
              MESH_DEBUG_PRINTLN("Init BME680 with BSEC Library version %d.%d.%d.%d (ULP mode)",
                                 BME680_BSEC.version.major, BME680_BSEC.version.minor,
                                 BME680_BSEC.version.major_bugfix, BME680_BSEC.version.minor_bugfix);
              BME680_initialized = true;
            }
          } else {
            MESH_DEBUG_PRINTLN("Init BME680 with BSEC Library version %d.%d.%d.%d (LP mode)",
                               BME680_BSEC.version.major, BME680_BSEC.version.minor,
                               BME680_BSEC.version.major_bugfix, BME680_BSEC.version.minor_bugfix);
            BME680_initialized = true;
          }
        } else {
          BME680_initialized = false;
          MESH_DEBUG_PRINTLN("BSEC initialization failed, status: %d", BME680_BSEC.status);
        }
      } else {
        BME680_initialized = false;
        MESH_DEBUG_PRINTLN("BME680 BSEC failed to begin at address %02X", TELEM_BME680_ADDRESS);
      }
#else
      // Initialize with Adafruit library (no IAQ)
      if (BME680.begin(TELEM_BME680_ADDRESS)) {
        MESH_DEBUG_PRINTLN("Found BME680 at address: %02X (Adafruit library)", TELEM_BME680_ADDRESS);

        // Configure BME680 oversampling and filter
        BME680.setTemperatureOversampling(BME680_OS_8X);
        BME680.setHumidityOversampling(BME680_OS_2X);
        BME680.setPressureOversampling(BME680_OS_4X);
        BME680.setIIRFilterSize(BME680_FILTER_SIZE_3);
        BME680.setGasHeater(320, 150); // 320°C for 150 ms

        BME680_initialized = true;
      } else {
        BME680_initialized = false;
        MESH_DEBUG_PRINTLN("BME680 chip detected but init failed at address %02X", TELEM_BME680_ADDRESS);
      }
#endif
    } else {
      BME680_initialized = false;
      MESH_DEBUG_PRINTLN("Not a BME680 at address %02X (chip ID: 0x%02X)", TELEM_BME680_ADDRESS, chip_id);
    }
  } else {
    BME680_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device at address %02X", TELEM_BME680_ADDRESS);
  }
#endif

#if ENV_INCLUDE_AHTX0
  // Check if device exists before trying to initialize
  if (i2c_device_exists(TELEM_WIRE, TELEM_AHTX_ADDRESS)) {
    if (AHTX0.begin(TELEM_WIRE, 0, TELEM_AHTX_ADDRESS)) {
      MESH_DEBUG_PRINTLN("Found AHT10/AHT20 at address: %02X", TELEM_AHTX_ADDRESS);
      AHTX0_initialized = true;
    } else {
      AHTX0_initialized = false;
      MESH_DEBUG_PRINTLN("AHT10/AHT20 did not initialize at I2C address %02X", TELEM_AHTX_ADDRESS);
    }
  } else {
    AHTX0_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (AHT10/AHT20)", TELEM_AHTX_ADDRESS);
  }
#endif

#if ENV_INCLUDE_BME280
  // Check chip ID: BME280=0x60, skip if BME680 (0x61) or BMP280 (0x58)
  if (i2c_device_exists(TELEM_WIRE, TELEM_BME280_ADDRESS)) {
    uint8_t chip_id = read_chip_id(TELEM_WIRE, TELEM_BME280_ADDRESS);

    if (chip_id == 0x60) { // BME280 chip ID
      if (BME280.begin(TELEM_BME280_ADDRESS, TELEM_WIRE)) {
        MESH_DEBUG_PRINTLN("Found BME280 at address: %02X (chip ID: 0x60)", TELEM_BME280_ADDRESS);
        BME280_initialized = true;
      } else {
        BME280_initialized = false;
        MESH_DEBUG_PRINTLN("BME280 chip detected but init failed at address %02X", TELEM_BME280_ADDRESS);
      }
    } else {
      BME280_initialized = false;
      MESH_DEBUG_PRINTLN("Not a BME280 at address %02X (chip ID: 0x%02X)", TELEM_BME280_ADDRESS, chip_id);
    }
  } else {
    BME280_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (BME280)", TELEM_BME280_ADDRESS);
  }
#endif

#if ENV_INCLUDE_BMP280
  // Check chip ID: BMP280=0x58, skip if BME680 (0x61) or BME280 (0x60)
  if (i2c_device_exists(TELEM_WIRE, TELEM_BMP280_ADDRESS)) {
    uint8_t chip_id = read_chip_id(TELEM_WIRE, TELEM_BMP280_ADDRESS);

    if (chip_id == 0x58) { // BMP280 chip ID
      if (BMP280.begin(TELEM_BMP280_ADDRESS)) {
        MESH_DEBUG_PRINTLN("Found BMP280 at address: %02X (chip ID: 0x58)", TELEM_BMP280_ADDRESS);
        BMP280_initialized = true;
      } else {
        BMP280_initialized = false;
        MESH_DEBUG_PRINTLN("BMP280 chip detected but init failed at address %02X", TELEM_BMP280_ADDRESS);
      }
    } else {
      BMP280_initialized = false;
      MESH_DEBUG_PRINTLN("Not a BMP280 at address %02X (chip ID: 0x%02X)", TELEM_BMP280_ADDRESS, chip_id);
    }
  } else {
    BMP280_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (BMP280)", TELEM_BMP280_ADDRESS);
  }
#endif

#if ENV_INCLUDE_SHTC3
  if (i2c_device_exists(TELEM_WIRE, 0x70)) {
    if (SHTC3.begin()) {
      MESH_DEBUG_PRINTLN("Found sensor: SHTC3");
      SHTC3_initialized = true;
    } else {
      SHTC3_initialized = false;
      MESH_DEBUG_PRINTLN("SHTC3 did not initialize at I2C address %02X", 0x70);
    }
  } else {
    SHTC3_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (SHTC3)", 0x70);
  }
#endif

#if ENV_INCLUDE_SHT4X
  if (i2c_device_exists(TELEM_WIRE, TELEM_SHT4X_ADDRESS)) {
    SHT4X.begin(*TELEM_WIRE, TELEM_SHT4X_ADDRESS);
    uint32_t serialNumber = 0;
    int16_t sht4x_error;
    sht4x_error = SHT4X.serialNumber(serialNumber);
    if (sht4x_error == 0) {
      MESH_DEBUG_PRINTLN("Found SHT4X at address: %02X", TELEM_SHT4X_ADDRESS);
      SHT4X_initialized = true;
    } else {
      SHT4X_initialized = false;
      MESH_DEBUG_PRINTLN("SHT4X did not initialize at I2C address %02X", TELEM_SHT4X_ADDRESS);
    }
  } else {
    SHT4X_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (SHT4X)", TELEM_SHT4X_ADDRESS);
  }
#endif

#if ENV_INCLUDE_LPS22HB
  if (i2c_device_exists(TELEM_WIRE, 0x5C)) {
    if (BARO.begin()) {
      MESH_DEBUG_PRINTLN("Found sensor: LPS22HB");
      LPS22HB_initialized = true;
    } else {
      LPS22HB_initialized = false;
      MESH_DEBUG_PRINTLN("LPS22HB did not initialize at I2C address %02X", 0x5C);
    }
  } else {
    LPS22HB_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (LPS22HB)", 0x5C);
  }
#endif

#if ENV_INCLUDE_INA3221
  if (INA3221.begin(TELEM_INA3221_ADDRESS, TELEM_WIRE)) {
    MESH_DEBUG_PRINTLN("Found INA3221 at address: %02X", TELEM_INA3221_ADDRESS);
    MESH_DEBUG_PRINTLN("%04X %04X", INA3221.getDieID(), INA3221.getManufacturerID());

    for (int i = 0; i < 3; i++) {
      INA3221.setShuntResistance(i, TELEM_INA3221_SHUNT_VALUE);
    }
    INA3221_initialized = true;
  } else {
    INA3221_initialized = false;
    MESH_DEBUG_PRINTLN("INA3221 was not found at I2C address %02X", TELEM_INA3221_ADDRESS);
  }
#endif

#if ENV_INCLUDE_INA219
  if (i2c_device_exists(TELEM_WIRE, TELEM_INA219_ADDRESS)) {
    if (INA219.begin(TELEM_WIRE)) {
      MESH_DEBUG_PRINTLN("Found INA219 at address: %02X", TELEM_INA219_ADDRESS);
      INA219_initialized = true;
    } else {
      INA219_initialized = false;
      MESH_DEBUG_PRINTLN("INA219 did not initialize at I2C address %02X", TELEM_INA219_ADDRESS);
    }
  } else {
    INA219_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (INA219)", TELEM_INA219_ADDRESS);
  }
#endif

#if ENV_INCLUDE_INA260
  if (i2c_device_exists(TELEM_WIRE, TELEM_INA260_ADDRESS)) {
    if (INA260.begin(TELEM_INA260_ADDRESS, TELEM_WIRE)) {
      MESH_DEBUG_PRINTLN("Found INA260 at address: %02X", TELEM_INA260_ADDRESS);
      INA260_initialized = true;
    } else {
      INA260_initialized = false;
      MESH_DEBUG_PRINTLN("INA260 did not initialize at I2C address %02X", TELEM_INA260_ADDRESS);
    }
  } else {
    INA260_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (INA260)", TELEM_INA260_ADDRESS);
  }
#endif

#if ENV_INCLUDE_INA226
  if (i2c_device_exists(TELEM_WIRE, TELEM_INA226_ADDRESS)) {
    if (INA226_sensor.begin()) {
      MESH_DEBUG_PRINTLN("Found INA226 at address: %02X", TELEM_INA226_ADDRESS);
      INA226_sensor.setMaxCurrentShunt(TELEM_INA226_MAX_AMP, TELEM_INA226_SHUNT_VALUE);
      INA226_initialized = true;
    } else {
      INA226_initialized = false;
      MESH_DEBUG_PRINTLN("INA226 did not initialize at I2C address %02X", TELEM_INA226_ADDRESS);
    }
  } else {
    INA226_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (INA226)", TELEM_INA226_ADDRESS);
  }
#endif

#if ENV_INCLUDE_MLX90614
  if (i2c_device_exists(TELEM_WIRE, TELEM_MLX90614_ADDRESS)) {
    if (MLX90614.begin(TELEM_MLX90614_ADDRESS, TELEM_WIRE)) {
      MESH_DEBUG_PRINTLN("Found MLX90614 at address: %02X", TELEM_MLX90614_ADDRESS);
      MLX90614_initialized = true;
    } else {
      MLX90614_initialized = false;
      MESH_DEBUG_PRINTLN("MLX90614 did not initialize at I2C address %02X", TELEM_MLX90614_ADDRESS);
    }
  } else {
    MLX90614_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (MLX90614)", TELEM_MLX90614_ADDRESS);
  }
#endif

#if ENV_INCLUDE_VL53L0X
  if (i2c_device_exists(TELEM_WIRE, TELEM_VL53L0X_ADDRESS)) {
    if (VL53L0X.begin(TELEM_VL53L0X_ADDRESS, false, TELEM_WIRE)) {
      MESH_DEBUG_PRINTLN("Found VL53L0X at address: %02X", TELEM_VL53L0X_ADDRESS);
      VL53L0X_initialized = true;
    } else {
      VL53L0X_initialized = false;
      MESH_DEBUG_PRINTLN("VL53L0X did not initialize at I2C address %02X", TELEM_VL53L0X_ADDRESS);
    }
  } else {
    VL53L0X_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (VL53L0X)", TELEM_VL53L0X_ADDRESS);
  }
#endif

#if ENV_INCLUDE_BMP085
  // Skip BMP085 if BME680 is using address 0x77
#if ENV_INCLUDE_BME680 && (TELEM_BME680_ADDRESS == 0x77 || TELEM_BME680_ADDRESS == 0x76)
  BMP085_initialized = false;
  MESH_DEBUG_PRINTLN("Skipping BMP085 - BME680 is using address 0x77");
#else
  // First argument is  MODE (aka oversampling) choose ULTRALOWPOWER
  if (i2c_device_exists(TELEM_WIRE, 0x77)) {
    if (BMP085.begin(0, TELEM_WIRE)) {
      MESH_DEBUG_PRINTLN("Found sensor BMP085");
      BMP085_initialized = true;
    } else {
      BMP085_initialized = false;
      MESH_DEBUG_PRINTLN("BMP085 did not initialize at I2C address %02X", 0x77);
    }
  } else {
    BMP085_initialized = false;
    MESH_DEBUG_PRINTLN("No I2C device found at address %02X (BMP085)", 0x77);
  }
#endif
#endif

  return true;
}

bool EnvironmentSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP &telemetry) {
  // I2C bus recovery voordat we sensors uitlezen
#if ENV_PIN_SDA && ENV_PIN_SCL && defined(ESP32)
  // ESP32 TwoWire heeft geen busy() methode, gebruik alternatieve check
  static uint32_t last_i2c_error_time = 0;
  static uint8_t consecutive_errors = 0;

  // Als we recent veel I2C errors hadden, probeer recovery
  if (consecutive_errors > 3 && (millis() - last_i2c_error_time) < 5000) {
    MESH_DEBUG_PRINTLN("I2C instability detected, attempting recovery...");

    Wire1.end();
    delay(10);
    Wire1.begin(ENV_PIN_SDA, ENV_PIN_SCL, 100000);
    delay(50);

    consecutive_errors = 0;
    last_i2c_error_time = millis();
  }
#endif

  next_available_channel = TELEM_CHANNEL_SELF + 1;

  if (requester_permissions & TELEM_PERM_LOCATION && gps_active) {
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon,
                     node_altitude); // allow lat/lon via telemetry even if no GPS is detected
  }

  if (requester_permissions & TELEM_PERM_ENVIRONMENT) {

#if ENV_INCLUDE_AHTX0
    if (AHTX0_initialized) {
      sensors_event_t humidity, temp;
      AHTX0.getEvent(&humidity, &temp);
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temp.temperature);
      telemetry.addRelativeHumidity(TELEM_CHANNEL_SELF, humidity.relative_humidity);
    }
#endif

#if ENV_INCLUDE_BME680
    if (BME680_initialized) {
      // Use cached data from loop()
      telemetry.addTemperature(TELEM_CHANNEL_SELF, bme680_temperature);
      telemetry.addRelativeHumidity(TELEM_CHANNEL_SELF, bme680_humidity);
      telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, bme680_pressure);
      telemetry.addAltitude(TELEM_CHANNEL_SELF, bme680_altitude);
      telemetry.addGenericSensor(next_available_channel, bme680_gas_resistance);

#ifdef BME680_BSEC2_SUPPORTED
      next_available_channel++;
      telemetry.addGenericSensor(next_available_channel, bme680_iaq);
      telemetry.addGenericSensor(next_available_channel, bme680_iaq_accuracy);
#endif
      next_available_channel++;
    }
#endif

#if ENV_INCLUDE_BME280
    if (BME280_initialized) {
      float temp_reading = BME280.readTemperature();
      // Check if reading is valid (not NaN or unreasonable value)
      if (!isnan(temp_reading) && temp_reading > -40 && temp_reading < 85) {
        telemetry.addTemperature(TELEM_CHANNEL_SELF, temp_reading);
        telemetry.addRelativeHumidity(TELEM_CHANNEL_SELF, BME280.readHumidity());
        telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, BME280.readPressure() / 100);
        telemetry.addAltitude(TELEM_CHANNEL_SELF, BME280.readAltitude(TELEM_BME280_SEALEVELPRESSURE_HPA));
      } else {
        // Sensor not responding properly, mark as uninitialized
        BME280_initialized = false;
        MESH_DEBUG_PRINTLN("BME280 read failed, disabling sensor");
      }
    }
#endif

#if ENV_INCLUDE_BMP280
    if (BMP280_initialized) {
      telemetry.addTemperature(TELEM_CHANNEL_SELF, BMP280.readTemperature());
      telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, BMP280.readPressure() / 100);
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
      for (int i = 0; i < TELEM_INA3221_NUM_CHANNELS; i++) {
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
      telemetry.addVoltage(next_available_channel, INA226_sensor.getBusVoltage());
      telemetry.addCurrent(next_available_channel, INA226_sensor.getCurrent_mA() / 1000.0);
      telemetry.addPower(next_available_channel, INA226_sensor.getPower_mW() / 1000.0);
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
      if (measure.RangeStatus != 4) {       // phase failures
        telemetry.addDistance(TELEM_CHANNEL_SELF, measure.RangeMilliMeter / 1000.0f); // convert mm to m
      } else {
        telemetry.addDistance(TELEM_CHANNEL_SELF, 0.0f); // no valid measurement
      }
    }
#endif

#if ENV_INCLUDE_BMP085
    if (BMP085_initialized) {
      telemetry.addTemperature(TELEM_CHANNEL_SELF, BMP085.readTemperature());
      telemetry.addBarometricPressure(TELEM_CHANNEL_SELF, BMP085.readPressure() / 100);
      telemetry.addAltitude(TELEM_CHANNEL_SELF, BMP085.readAltitude(TELEM_BMP085_SEALEVELPRESSURE_HPA * 100));
    }
#endif
  }

#if ENV_PIN_SDA && ENV_PIN_SCL && defined(ESP32)
  // Reset error counter als alles goed ging
  consecutive_errors = 0;
#endif

  return true;
}

int EnvironmentSensorManager::getNumSettings() const {
  int settings = 0;
#if ENV_INCLUDE_GPS
  if (gps_detected) settings++; // only show GPS setting if GPS is detected
#endif
  return settings;
}

const char *EnvironmentSensorManager::getSettingName(int i) const {
  int settings = 0;
#if ENV_INCLUDE_GPS
  if (gps_detected && i == settings++) {
    return "gps";
  }
#endif
  // convenient way to add params (needed for some tests)
  //  if (i == settings++) return "param.2";
  return NULL;
}

const char *EnvironmentSensorManager::getSettingValue(int i) const {
  int settings = 0;
#if ENV_INCLUDE_GPS
  if (gps_detected && i == settings++) {
    return gps_active ? "1" : "0";
  }
#endif
  // convenient way to add params ...
  //  if (i == settings++) return "2";
  return NULL;
}

bool EnvironmentSensorManager::setSettingValue(const char *name, const char *value) {
#if ENV_INCLUDE_GPS
  if (gps_detected && strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
#endif
  return false; // not supported
}

#if ENV_INCLUDE_GPS
void EnvironmentSensorManager::initBasicGPS() {
  // Initialize serial port with correct pins
  MESH_DEBUG_PRINTLN("PIN_GPS_TX %d, PIN_GPS_RX %d", PIN_GPS_TX, PIN_GPS_RX);

#ifdef GPS_BAUD_RATE
  Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  MESH_DEBUG_PRINTLN("GPS Serial initialized at %d baud", GPS_BAUD_RATE);
#else
  Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  MESH_DEBUG_PRINTLN("GPS Serial initialized at 9600 baud");
#endif

  delay(1000); // Give GPS time to initialize

  // Try to detect if GPS is physically connected to determine if we should expose the setting
  _location->begin();
  _location->reset();

#ifndef PIN_GPS_EN
  MESH_DEBUG_PRINTLN("No GPS wake/reset pin found for this board. Continuing on...");
#endif

  // On Heltec boards, assume GPS is present if we have a board pointer
  // We can't power it on during init because board.begin() hasn't been called yet
#ifdef ENV_SKIP_GPS_DETECT
  gps_detected = true;
  MESH_DEBUG_PRINTLN("GPS detection skipped (ENV_SKIP_GPS_DETECT)");
#elif defined(HELTEC_LORA_V3)
  gps_detected = (_board != nullptr);
  if (gps_detected) {
    MESH_DEBUG_PRINTLN("Heltec: GPS assumed present (board configured with GPS)");
    MESH_DEBUG_PRINTLN("GPS detection check: Serial1.available() = %d", Serial1.available());

  } else {
    MESH_DEBUG_PRINTLN("Heltec: No board reference, GPS not configured");
  }
#else
  // For non-Heltec boards, try to detect GPS by checking for data
  delay(1000);
  gps_detected = (Serial1.available() > 0);
  MESH_DEBUG_PRINTLN("GPS detection check: Serial1.available() = %d", Serial1.available());
#endif

  if (gps_detected) {
    MESH_DEBUG_PRINTLN("GPS detected");
#ifdef PERSISTANT_GPS
    // Mark GPS for deferred power-on in persistent mode
    // Actual power-on happens in loop() after board.begin() has been called
    gps_active = false; // Not active yet - will be activated in loop()
    gps_needs_power_on = true;
    MESH_DEBUG_PRINTLN("GPS marked for persistent mode (deferred power-on in loop)");
    return;
#endif
  } else {
    MESH_DEBUG_PRINTLN("No GPS detected");
  }

  _location->stop();
  gps_active = false; // Set GPS visibility off until setting is changed
}

// gps code for rak might be moved to MicroNMEALoactionProvider
// or make a new location provider ...
#ifdef RAK_WISBLOCK_GPS
void EnvironmentSensorManager::rakGPSInit() {

  Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);

#ifdef GPS_BAUD_RATE
  Serial1.begin(GPS_BAUD_RATE);
#else
  Serial1.begin(9600);
#endif

  // search for the correct IO standby pin depending on socket used
  if (gpsIsAwake(WB_IO2)) {
    //  MESH_DEBUG_PRINTLN("RAK base board is RAK19007/10");
    //  MESH_DEBUG_PRINTLN("GPS is installed on Socket A");
  } else if (gpsIsAwake(WB_IO4)) {
    //  MESH_DEBUG_PRINTLN("RAK base board is RAK19003/9");
    //  MESH_DEBUG_PRINTLN("GPS is installed on Socket C");
  } else if (gpsIsAwake(WB_IO5)) {
    //  MESH_DEBUG_PRINTLN("RAK base board is RAK19001/11");
    //  MESH_DEBUG_PRINTLN("GPS is installed on Socket F");
  } else {
    MESH_DEBUG_PRINTLN("No GPS found");
    gps_active = false;
    gps_detected = false;
    return;
  }

#ifndef FORCE_GPS_ALIVE // for use with repeaters, until GPS toggle is implimented
  // Now that GPS is found and set up, set to sleep for initial state
  stop_gps();
#endif
}

bool EnvironmentSensorManager::gpsIsAwake(uint8_t ioPin) {

  // set initial waking state
  pinMode(ioPin, OUTPUT);
  digitalWrite(ioPin, LOW);
  delay(500);
  digitalWrite(ioPin, HIGH);
  delay(500);

  // Try to init RAK12500 on I2C
  if (ublox_GNSS.begin(Wire) == true) {
    MESH_DEBUG_PRINTLN("RAK12500 GPS init correctly with pin %i", ioPin);
    ublox_GNSS.setI2COutput(COM_TYPE_UBX);
    ublox_GNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_GPS);
    ublox_GNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_GALILEO);
    ublox_GNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_GLONASS);
    ublox_GNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_SBAS);
    ublox_GNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_BEIDOU);
    ublox_GNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_IMES);
    ublox_GNSS.enableGNSS(true, SFE_UBLOX_GNSS_ID_QZSS);
    ublox_GNSS.setMeasurementRate(1000);
    ublox_GNSS.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT);
    gpsResetPin = ioPin;
    i2cGPSFlag = true;
    gps_active = true;
    gps_detected = true;

    _location = &RAK12500_provider;
    return true;
  } else if (Serial1) {
    MESH_DEBUG_PRINTLN("Serial GPS init correctly and is turned on");
    if (PIN_GPS_EN) {
      gpsResetPin = PIN_GPS_EN;
    }
    serialGPSFlag = true;
    gps_active = true;
    gps_detected = true;
    return true;
  }
  MESH_DEBUG_PRINTLN("GPS did not init with this IO pin... try the next");
  return false;
}
#endif

void EnvironmentSensorManager::start_gps() {
  if (!gps_active) {
#ifdef RAK_WISBLOCK_GPS
    pinMode(gpsResetPin, OUTPUT);
    digitalWrite(gpsResetPin, HIGH);
    gps_active = true;
    return;
#endif

#ifdef HELTEC_LORA_V3
    // Heltec boards: manage peripheral power for GPS
    if (_board) {
      _board->periph_power.claim();
      MESH_DEBUG_PRINTLN("Heltec: Claimed peripheral power for GPS");
      digitalWrite(PIN_GPS_EN, HIGH); // Wake GPS from standby

      delay(1000);


      // Send configuration command to Heltec GPS modules
      Serial1.println("$CFGSYS,h35155*68");
      MESH_DEBUG_PRINTLN("Heltec: GPS configuration command sent");
      MESH_DEBUG_PRINTLN("Waiting for gps to power up");
      delay(1000);
      gps_active = true;
    }
#endif

    _location->begin();
    _location->reset();

#ifndef PIN_GPS_RESET
    MESH_DEBUG_PRINTLN("Start GPS (no reset pin on this board)");
#endif
  }
}

void EnvironmentSensorManager::stop_gps() {
  if (gps_active) {
    gps_active = false;

#ifdef RAK_WISBLOCK_GPS
    pinMode(gpsResetPin, OUTPUT);
    digitalWrite(gpsResetPin, LOW);
    return;
#endif

    _location->stop();

#ifdef HELTEC_LORA_V3
    // Heltec boards: release peripheral power for GPS
    if (_board) {
      _board->periph_power.release();
      MESH_DEBUG_PRINTLN("Heltec: Released peripheral power for GPS");
    }
#endif

#ifndef PIN_GPS_EN
    MESH_DEBUG_PRINTLN("Stop GPS (no enable pin on this board)");
#endif
  }
}
#endif

#if ENV_INCLUDE_GPS || ENV_INCLUDE_BME680
void EnvironmentSensorManager::loop() {
  static long next_gps_update = 0;

#if ENV_INCLUDE_GPS
  // Handle deferred GPS power-on for PERSISTANT_GPS mode
  // This ensures board.begin() has been called before we use periph_power
  if (gps_needs_power_on && !gps_active) {
    MESH_DEBUG_PRINTLN("Activating GPS (deferred from init)...");
    start_gps();
    gps_needs_power_on = false;
  }

  _location->loop();

  if (millis() > next_gps_update) {
    if (gps_active) {
      static bool last_valid_state = false;
      static long last_sat_count = 0;
      bool currently_valid = _location->isValid();
      long current_sats = _location->satellitesCount();

      // Log status changes or periodic updates (every 10 seconds when no fix)
      static unsigned long last_status_log = 0;
      bool should_log = (currently_valid != last_valid_state) || (current_sats != last_sat_count) ||
                        (!currently_valid && (millis() - last_status_log > 10000));

#ifdef RAK_WISBLOCK_GPS
      if ((i2cGPSFlag || serialGPSFlag) && _location->isValid()) {
        if (should_log) {
          MESH_DEBUG_PRINTLN("GPS FIX ACQUIRED: lat %.6f, lon %.6f, alt %.1fm, sats %ld",
                             ((double)_location->getLatitude()) / 1000000.,
                             ((double)_location->getLongitude()) / 1000000.,
                             ((double)_location->getAltitude()) / 1000.0, current_sats);
          last_status_log = millis();
        }
        node_lat = ((double)_location->getLatitude()) / 1000000.;
        node_lon = ((double)_location->getLongitude()) / 1000000.;
        node_altitude = ((double)_location->getAltitude()) / 1000.0;
      } else if (should_log) {
        MESH_DEBUG_PRINTLN("GPS searching... (sats: %ld, valid: %s)", current_sats,
                           _location->isValid() ? "YES" : "NO");
        last_status_log = millis();
      }
#else
      if (_location->isValid()) {
        if (should_log) {
          MESH_DEBUG_PRINTLN("GPS FIX ACQUIRED: lat %.6f, lon %.6f, alt %.1fm, sats %ld",
                             ((double)_location->getLatitude()) / 1000000.,
                             ((double)_location->getLongitude()) / 1000000.,
                             ((double)_location->getAltitude()) / 1000.0, current_sats);
          last_status_log = millis();
        }
        node_lat = ((double)_location->getLatitude()) / 1000000.;
        node_lon = ((double)_location->getLongitude()) / 1000000.;
        node_altitude = ((double)_location->getAltitude()) / 1000.0;
      } else if (should_log) {
        MESH_DEBUG_PRINTLN("GPS searching... (sats: %ld, valid: %s)", current_sats,
                           _location->isValid() ? "YES" : "NO");
        last_status_log = millis();
      }
#endif

      last_valid_state = currently_valid;
      last_sat_count = current_sats;
    }
    next_gps_update = millis() + 1000;
  }
#endif

#if ENV_INCLUDE_BME680
  //  Periodically read BME680 sensor and cache the values
  unsigned long now = millis();

  if (BME680_initialized) {
#ifdef BME680_BSEC2_SUPPORTED
    if (now - last_bme680_reading >= 3000 || last_bme680_reading == 0) {
      // BSEC2: Call run() to get new data
      if (!BME680_BSEC.run()) {
        MESH_DEBUG_PRINTLN("BSEC run failed, status: %d", BME680_BSEC.status);
      } else {
        // Get sensor data from BSEC
        bsecData temp_data = BME680_BSEC.getData(BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE);
        bsecData hum_data = BME680_BSEC.getData(BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY);
        bsecData press_data = BME680_BSEC.getData(BSEC_OUTPUT_RAW_PRESSURE);
        bsecData gas_data = BME680_BSEC.getData(BSEC_OUTPUT_RAW_GAS);
        bsecData iaq_data = BME680_BSEC.getData(BSEC_OUTPUT_IAQ);

        if (temp_data.signal != 0 && press_data.signal != 0) {
          bme680_temperature = temp_data.signal;
          bme680_humidity = hum_data.signal;
          bme680_pressure = press_data.signal / 100.0;      // Convert Pa to hPa
          bme680_gas_resistance = gas_data.signal / 1000.0; // Convert Ohm to kOhm
          bme680_iaq = iaq_data.signal;
          bme680_iaq_accuracy = iaq_data.accuracy;

          // Calculate altitude from pressure + geoidal separation correction
          float alt_msl = 44330.0 * (1.0 - pow(bme680_pressure / TELEM_BME680_SEALEVELPRESSURE_HPA, 0.1903));

          // Determine geoidal separation
          float geoid_correction = GEOIDAL_SEPARATION; // Default

#if ENV_INCLUDE_GPS
          // Use GPS coordinates if available (from actual GPS fix)
          if (gps_active && _location->isValid() && node_lat != 0.0 && node_lon != 0.0) {
            geoid_correction = get_geoidal_separation(node_lat, node_lon);
            MESH_DEBUG_PRINTLN("Using GPS geoid: lat=%.2f, lon=%.2f -> %.1fm", node_lat, node_lon,
                               geoid_correction);
          } else if (geoid_correction == 0.0 && node_lat != 0.0 && node_lon != 0.0) {
            // Fall back to compile-time advertised location if GPS not available
            geoid_correction = get_geoidal_separation(node_lat, node_lon);
            MESH_DEBUG_PRINTLN("Using advertised location geoid: lat=%.2f, lon=%.2f, alt=%.2f. raw_alt=%.2f, "
                               "corr_alt=%.02f -> %.1fm",
                               (double)node_lat, (double)node_lon, (double)node_altitude, alt_msl,
                               (alt_msl - geoid_correction), geoid_correction);
          }
#endif

          bme680_altitude = alt_msl - geoid_correction;
          last_bme680_reading = now;

          MESH_DEBUG_PRINTLN(
              "BME680 (BSEC2): Temp=%.2f°C, Hum=%.2f%%, Press=%.2f hPa, Raw_Alt=%.2f, Corr_Alt=%.2fm "
              "(geoid:%.1fm), Gas=%.0f kOhm, "
              "IAQ=%.2f, Acc=%d",
              bme680_temperature, bme680_humidity, bme680_pressure, alt_msl, bme680_altitude,
              geoid_correction, bme680_gas_resistance, bme680_iaq, (int)bme680_iaq_accuracy);
        }
      }
    }
#else
    // Adafruit library: Read periodically (every 3 seconds)

    if (now - last_bme680_reading >= 3000 || last_bme680_reading == 0) {
      BME680.begin();
      if (BME680.performReading()) {
        bme680_temperature = BME680.temperature;
        bme680_humidity = BME680.humidity;
        bme680_pressure = BME680.pressure / 100; // Convert Pa to hPa
        bme680_gas_resistance = BME680.gas_resistance;

        // Use library's altitude calculation + geoidal separation correction
        float alt_msl = BME680.readAltitude(TELEM_BME680_SEALEVELPRESSURE_HPA);

        // Determine geoidal separation
        float geoid_correction = GEOIDAL_SEPARATION; // Default

#if ENV_INCLUDE_GPS
        // Use GPS coordinates if available (from actual GPS fix)
        if (gps_active && _location->isValid() && node_lat != 0.0 && node_lon != 0.0) {
          geoid_correction = get_geoidal_separation(node_lat, node_lon);
          MESH_DEBUG_PRINTLN("Using GPS geoid: lat=%.2f, lon=%.2f -> %.1fm", node_lat, node_lon,
                             geoid_correction);
        } else if (geoid_correction == 0.0 && node_lat != 0.0 && node_lon != 0.0) {
          // Fall back to compile-time advertised location if GPS not available
          geoid_correction = get_geoidal_separation(node_lat, node_lon);
          MESH_DEBUG_PRINTLN("Using advertised location geoid: lat=%.2f, lon=%.2f -> %.1fm", (double)node_lat,
                             (double)node_lon, geoid_correction);
        }
#endif

        bme680_altitude = alt_msl - geoid_correction;
        last_bme680_reading = now;

        MESH_DEBUG_PRINTLN("BME680 (Adafruit): Temp=%.1f°C, Hum=%.1f%%, Press=%.1f hPa, Alt=%.1fm "
                           "(geoid:%.1fm), Gas=%.0f Ohm",
                           bme680_temperature, bme680_humidity, bme680_pressure, bme680_altitude,
                           geoid_correction, bme680_gas_resistance);
      } else {
        MESH_DEBUG_PRINTLN("BME680 reading failed, keeping previous values");
      }
    }
#endif
  }
#endif
}
#endif