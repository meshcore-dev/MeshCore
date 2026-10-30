#include "EnvironmentSensorManager.h"

#include <Wire.h>
#if ENV_INCLUDE_GPS && defined(GPS_GUARD)
#include <RTClib.h>
#endif

#if ENV_PIN_SDA && ENV_PIN_SCL
#define TELEM_WIRE &Wire1  // Use Wire1 as the I2C bus for Environment Sensors
#else
#define TELEM_WIRE &Wire  // Use default I2C bus for Environment Sensors
#endif

// ============================================================
// Sensor library includes and static driver instances
// ============================================================

#if ENV_INCLUDE_BME680_BSEC
#ifndef TELEM_BME680_ADDRESS
#define TELEM_BME680_ADDRESS 0x76
#endif
#define TELEM_BME680_SEALEVELPRESSURE_HPA (1013.25)
#include <bsec.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
static const uint8_t bsec_config_iaq[] = {
#include "config/generic_33v_3s_28d/bsec_iaq.txt" // 3.3v, LP, 28 day background calibration window
};
static Bsec     bsec_iaq;
static float    bsec_temperature     = 0;
static float    bsec_humidity        = 0;
static float    bsec_pressure_hpa    = 0;
static float    bsec_iaq_val         = 0;
static uint8_t  bsec_accuracy        = 0;
static bool     bsec_active          = false;
static bool     bsec_data_ready      = false;
static bool     bsec_first_save_done = false;
static uint32_t bsec_last_save_ms    = 0;
#define BSEC_STATE_FILE "/bsec_state.bin"
#define BSEC_SAVE_INTERVAL_MS (8UL * 60 * 60 * 1000) // 8 hour state-save interval
#endif

#ifdef ENV_INCLUDE_BME680
#ifndef TELEM_BME680_ADDRESS
#define TELEM_BME680_ADDRESS 0x76
#endif
#define TELEM_BME680_SEALEVELPRESSURE_HPA (1013.25)
#include <Adafruit_BME680.h>
static Adafruit_BME680 BME680(TELEM_WIRE);
#endif

#ifdef ENV_INCLUDE_BMP085
#define TELEM_BMP085_SEALEVELPRESSURE_HPA (1013.25)
#include <Adafruit_BMP085.h>
static Adafruit_BMP085 BMP085;
#endif

#if ENV_INCLUDE_AHTX0
#ifndef TELEM_AHTX_ADDRESS
#define TELEM_AHTX_ADDRESS      0x38      // AHT10, AHT20 temperature and humidity sensor I2C address
#endif
#include <Adafruit_AHTX0.h>
static Adafruit_AHTX0 AHTX0;
#endif

#if ENV_INCLUDE_BME280
#ifndef TELEM_BME280_ADDRESS
#define TELEM_BME280_ADDRESS    0x76      // BME280 environmental sensor I2C address
#endif
#define TELEM_BME280_SEALEVELPRESSURE_HPA (1013.25)    // Atmospheric pressure at sea level
#include <Adafruit_BME280.h>
static Adafruit_BME280 BME280;
#endif

#if ENV_INCLUDE_BMP280
#ifndef TELEM_BMP280_ADDRESS
#define TELEM_BMP280_ADDRESS    0x76      // BMP280 environmental sensor I2C address
#endif
#define TELEM_BMP280_SEALEVELPRESSURE_HPA (1013.25)    // Atmospheric pressure at sea level
#include <Adafruit_BMP280.h>
static Adafruit_BMP280 BMP280(TELEM_WIRE);
#endif

#if ENV_INCLUDE_SHTC3
#include <Adafruit_SHTC3.h>
static Adafruit_SHTC3 SHTC3;
#endif

#if ENV_INCLUDE_SHT4X
#ifndef TELEM_SHT4X_ADDRESS
#define TELEM_SHT4X_ADDRESS 0x44
#endif
#include <SensirionI2cSht4x.h>
static SensirionI2cSht4x SHT4X;
#endif

#if ENV_INCLUDE_LPS22HB
#include <Arduino_LPS22HB.h>
LPS22HBClass LPS22HB(*TELEM_WIRE);
#endif

#if ENV_INCLUDE_INA3221
#ifndef TELEM_INA3221_ADDRESS
#define TELEM_INA3221_ADDRESS     0x42    // INA3221 3 channel current sensor I2C address
#endif
#ifndef TELEM_INA3221_SHUNT_VALUE
#define TELEM_INA3221_SHUNT_VALUE 0.100 // most variants will have a 0.1 ohm shunts
#endif
#ifndef TELEM_INA3221_NUM_CHANNELS
#define TELEM_INA3221_NUM_CHANNELS 3
#endif
#include <Adafruit_INA3221.h>
static Adafruit_INA3221 INA3221;
#endif

#if ENV_INCLUDE_INA219
#ifndef TELEM_INA219_ADDRESS
#define TELEM_INA219_ADDRESS    0x40      // INA219 single channel current sensor I2C address
#endif
#include <Adafruit_INA219.h>
static Adafruit_INA219 INA219(TELEM_INA219_ADDRESS);
#endif

#if ENV_INCLUDE_INA260
#ifndef TELEM_INA260_ADDRESS
#define TELEM_INA260_ADDRESS    0x41      // INA260 single channel current sensor I2C address
#endif
#include <Adafruit_INA260.h>
static Adafruit_INA260 INA260;
#endif

#if ENV_INCLUDE_INA226
#ifndef TELEM_INA226_ADDRESS
#define TELEM_INA226_ADDRESS     0x44
#endif
#define TELEM_INA226_SHUNT_VALUE 0.100
#define TELEM_INA226_MAX_AMP     0.8
#include <INA226.h>
static INA226 INA226(TELEM_INA226_ADDRESS, TELEM_WIRE);
#endif

#if ENV_INCLUDE_MLX90614
#ifndef TELEM_MLX90614_ADDRESS
#define TELEM_MLX90614_ADDRESS 0x5A      // MLX90614 IR temperature sensor I2C address
#endif
#include <Adafruit_MLX90614.h>
static Adafruit_MLX90614 MLX90614;
#endif

#if ENV_INCLUDE_VL53L0X
#ifndef TELEM_VL53L0X_ADDRESS
#define TELEM_VL53L0X_ADDRESS 0x29      // VL53L0X time-of-flight distance sensor I2C address
#endif
#include <Adafruit_VL53L0X.h>
static Adafruit_VL53L0X VL53L0X;
#endif

#if ENV_INCLUDE_RAK12035
#ifndef TELEM_RAK12035_ADDRESS
#define TELEM_RAK12035_ADDRESS 0x20      // RAK12035 Soil Moisture sensor I2C address
#endif
#include "RAK12035_SoilMoisture.h"
static RAK12035_SoilMoisture RAK12035;
#endif

#if ENV_INCLUDE_GPS && defined(RAK_BOARD) && !defined(RAK_WISMESH_TAG)
#define RAK_WISBLOCK_GPS
#endif

#ifdef RAK_WISBLOCK_GPS
static uint32_t gpsResetPin = 0;
static bool i2cGPSFlag = false;
static bool serialGPSFlag = false;
#ifndef TELEM_RAK12500_ADDRESS
#define TELEM_RAK12500_ADDRESS   0x42     //RAK12500 Ublox GPS via i2c
#endif
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
  void sendSentence(const char * sentence) override { }
  void reset() override { }
  void begin() override { }
  void stop() override { }
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

// ============================================================
// I2C bus scanner
// Probes every valid address and records which ones ACK.
// This runs before any sensor library is touched, so a missing
// or misbehaving device cannot stall or crash the boot sequence.
// ============================================================

static void scanI2CBus(TwoWire* wire, bool found[128]) {
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    wire->beginTransmission(addr);
    found[addr] = (wire->endTransmission() == 0);
  }
}

// ============================================================
// Per-sensor init and query functions
//
// init(wire, address) — called only when the address was seen
//   on the bus. Returns 0 on failure, or the number of
//   telemetry channels the sensor will consume (1 for all
//   single-output sensors; INA3221 returns one per enabled
//   hardware channel; MLX90614 and RAK12035+calibration
//   return 2).
//
// query(channel, sub_channel, lpp) — called once per active
//   sensor entry during querySensors(). sub_channel is always
//   0 for single-output sensors.
// ============================================================

#if ENV_INCLUDE_AHTX0
static uint8_t init_ahtx0(TwoWire* wire, uint8_t addr) {
  return AHTX0.begin(wire, 0, addr) ? 1 : 0;
}
static void query_ahtx0(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  sensors_event_t humidity, temp;
  AHTX0.getEvent(&humidity, &temp);
  lpp.addTemperature(ch, temp.temperature);
  lpp.addRelativeHumidity(ch, humidity.relative_humidity);
}
#endif

#ifdef ENV_INCLUDE_BME680
static uint8_t init_bme680(TwoWire*, uint8_t addr) {
  // Wire was set in the static constructor; begin() takes address only.
  return BME680.begin(addr) ? 1 : 0;
}
static void query_bme680(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  if (BME680.performReading()) {
    lpp.addTemperature(ch, BME680.temperature);
    lpp.addRelativeHumidity(ch, BME680.humidity);
    const float pressure_hpa = BME680.pressure / 100.0f;
    lpp.addBarometricPressure(ch, pressure_hpa);
    lpp.addAltitude(ch, 44330.0f * (1.0f - powf(pressure_hpa / (float)TELEM_BME680_SEALEVELPRESSURE_HPA, 0.1903f)));
    lpp.addGenericSensor(ch, BME680.gas_resistance);
  }
}
#endif

#if ENV_INCLUDE_BME280
static uint8_t init_bme280(TwoWire* wire, uint8_t addr) {
  if (!BME280.begin(addr, wire)) return 0;
  BME280.setSampling(Adafruit_BME280::MODE_FORCED,
                     Adafruit_BME280::SAMPLING_X1,
                     Adafruit_BME280::SAMPLING_X1,
                     Adafruit_BME280::SAMPLING_X1,
                     Adafruit_BME280::FILTER_OFF,
                     Adafruit_BME280::STANDBY_MS_1000);
  return 1;
}
static void query_bme280(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  if (BME280.takeForcedMeasurement()) {
    lpp.addTemperature(ch, BME280.readTemperature());
    lpp.addRelativeHumidity(ch, BME280.readHumidity());
    lpp.addBarometricPressure(ch, BME280.readPressure() / 100);
    lpp.addAltitude(ch, BME280.readAltitude(TELEM_BME280_SEALEVELPRESSURE_HPA));
  }
}
#endif

#if ENV_INCLUDE_BMP280
static uint8_t init_bmp280(TwoWire*, uint8_t addr) {
  // BMP280 static instance was constructed with TELEM_WIRE; begin() uses it.
  return BMP280.begin(addr) ? 1 : 0;
}
static void query_bmp280(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  lpp.addTemperature(ch, BMP280.readTemperature());
  lpp.addBarometricPressure(ch, BMP280.readPressure() / 100);
  lpp.addAltitude(ch, BMP280.readAltitude(TELEM_BMP280_SEALEVELPRESSURE_HPA));
}
#endif

#if ENV_INCLUDE_SHTC3
static uint8_t init_shtc3(TwoWire* wire, uint8_t) {
  // Adafruit_SHTC3::begin() does not accept an address (fixed at 0x70).
  return SHTC3.begin(wire) ? 1 : 0;
}
static void query_shtc3(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  sensors_event_t humidity, temp;
  SHTC3.getEvent(&humidity, &temp);
  lpp.addTemperature(ch, temp.temperature);
  lpp.addRelativeHumidity(ch, humidity.relative_humidity);
}
#endif

#if ENV_INCLUDE_SHT4X
static uint8_t init_sht4x(TwoWire* wire, uint8_t addr) {
  // SensirionI2cSht4x::begin() does not probe the hardware; use serialNumber()
  // as the actual presence check since it performs a real I2C transaction.
  SHT4X.begin(*wire, addr);
  uint32_t serial = 0;
  return (SHT4X.serialNumber(serial) == 0) ? 1 : 0;
}
static void query_sht4x(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  float temperature, humidity;
  if (SHT4X.measureLowestPrecision(temperature, humidity) == 0) {
    lpp.addTemperature(ch, temperature);
    lpp.addRelativeHumidity(ch, humidity);
  }
}
#endif

#if ENV_INCLUDE_LPS22HB
static uint8_t init_lps22hb(TwoWire*, uint8_t) {
  // LPS22HBClass is constructed with the wire reference; begin() uses it.
  return LPS22HB.begin() ? 1 : 0;
}
static void query_lps22hb(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  lpp.addTemperature(ch, LPS22HB.readTemperature());
  lpp.addBarometricPressure(ch, LPS22HB.readPressure() * 10); // convert kPa to hPa
}
#endif

#if ENV_INCLUDE_INA3221
static uint8_t init_ina3221(TwoWire* wire, uint8_t addr) {
  if (!INA3221.begin(addr, wire)) return 0;
  for (int i = 0; i < TELEM_INA3221_NUM_CHANNELS; i++) {
    INA3221.setShuntResistance(i, TELEM_INA3221_SHUNT_VALUE);
  }
  // Each enabled hardware channel becomes its own telemetry channel.
  uint8_t enabled = 0;
  for (int i = 0; i < TELEM_INA3221_NUM_CHANNELS; i++) {
    if (INA3221.isChannelEnabled(i)) enabled++;
  }
  return enabled > 0 ? enabled : 1;
}
static void query_ina3221(uint8_t ch, uint8_t sub_ch, CayenneLPP& lpp) {
  // sub_ch is the index of the nth enabled hardware channel.
  uint8_t seen = 0;
  for (int i = 0; i < TELEM_INA3221_NUM_CHANNELS; i++) {
    if (INA3221.isChannelEnabled(i)) {
      if (seen == sub_ch) {
        float v = INA3221.getBusVoltage(i);
        float c = INA3221.getCurrentAmps(i);
        lpp.addVoltage(ch, v);
        lpp.addCurrent(ch, c);
        lpp.addPower(ch, v * c);
        return;
      }
      seen++;
    }
  }
}
#endif

#if ENV_INCLUDE_INA219
static uint8_t init_ina219(TwoWire* wire, uint8_t) {
  // INA219 static instance was constructed with the address; begin() uses it.
  return INA219.begin(wire) ? 1 : 0;
}
static void query_ina219(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  lpp.addVoltage(ch, INA219.getBusVoltage_V());
  lpp.addCurrent(ch, INA219.getCurrent_mA() / 1000.0f);
  lpp.addPower(ch, INA219.getPower_mW() / 1000.0f);
}
#endif

#if ENV_INCLUDE_INA260
static uint8_t init_ina260(TwoWire* wire, uint8_t addr) {
  return INA260.begin(addr, wire) ? 1 : 0;
}
static void query_ina260(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  lpp.addVoltage(ch, INA260.readBusVoltage() / 1000.0f);
  lpp.addCurrent(ch, INA260.readCurrent() / 1000.0f);
  lpp.addPower(ch, INA260.readPower() / 1000.0f);
}
#endif

#if ENV_INCLUDE_INA226
static uint8_t init_ina226(TwoWire*, uint8_t) {
  // INA226 static instance was constructed with address and wire.
  if (!INA226.begin()) return 0;
  INA226.setMaxCurrentShunt(TELEM_INA226_MAX_AMP, TELEM_INA226_SHUNT_VALUE);
  return 1;
}
static void query_ina226(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  lpp.addVoltage(ch, INA226.getBusVoltage());
  lpp.addCurrent(ch, INA226.getCurrent_mA() / 1000.0f);
  lpp.addPower(ch, INA226.getPower_mW() / 1000.0f);
}
#endif

#if ENV_INCLUDE_MLX90614
static uint8_t init_mlx90614(TwoWire* wire, uint8_t addr) {
  return MLX90614.begin(addr, wire) ? 2 : 0;  // 2 channels: object temp, ambient temp
}
static void query_mlx90614(uint8_t ch, uint8_t sub_ch, CayenneLPP& lpp) {
  if (sub_ch == 0)
    lpp.addTemperature(ch, MLX90614.readObjectTempC());
  else
    lpp.addTemperature(ch, MLX90614.readAmbientTempC());
}
#endif

#if ENV_INCLUDE_VL53L0X
static uint8_t init_vl53l0x(TwoWire* wire, uint8_t addr) {
  return VL53L0X.begin(addr, false, wire) ? 1 : 0;
}
static void query_vl53l0x(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  VL53L0X_RangingMeasurementData_t measure;
  VL53L0X.rangingTest(&measure, false);
  lpp.addDistance(ch, measure.RangeStatus != 4 ? measure.RangeMilliMeter / 1000.0f : 0.0f);
}
#endif

#ifdef ENV_INCLUDE_BMP085
static uint8_t init_bmp085(TwoWire* wire, uint8_t) {
  return BMP085.begin(0, wire) ? 1 : 0;  // mode 0 = ULTRALOWPOWER
}
static void query_bmp085(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  lpp.addTemperature(ch, BMP085.readTemperature());
  lpp.addBarometricPressure(ch, BMP085.readPressure() / 100);
  lpp.addAltitude(ch, BMP085.readAltitude(TELEM_BMP085_SEALEVELPRESSURE_HPA * 100));
}
#endif

#if ENV_INCLUDE_RAK12035
static uint8_t init_rak12035(TwoWire* wire, uint8_t addr) {
  // RAK12035 requires setup() before begin().
  RAK12035.setup(*wire);
  if (!RAK12035.begin(addr)) return 0;
#ifdef ENABLE_RAK12035_CALIBRATION
  return 2;  // moisture channel + calibration channel
#else
  return 1;
#endif
}
static void query_rak12035(uint8_t ch, uint8_t sub_ch, CayenneLPP& lpp) {
  if (sub_ch == 0) {
    lpp.addTemperature(ch, RAK12035.get_sensor_temperature());
    lpp.addPercentage(ch, RAK12035.get_sensor_moisture());
  } else {
#ifdef ENABLE_RAK12035_CALIBRATION
    float cap = RAK12035.get_sensor_capacitance();
    float wet = RAK12035.get_humidity_full();
    float dry = RAK12035.get_humidity_zero();
    lpp.addFrequency(ch, cap);
    lpp.addTemperature(ch, wet);
    lpp.addPower(ch, dry);
    if (cap > dry) RAK12035.set_humidity_zero(cap);
    if (cap < wet) RAK12035.set_humidity_full(cap);
#endif
  }
}
#endif

#if ENV_INCLUDE_BME680_BSEC
static void bsec_load_state() {
  using namespace Adafruit_LittleFS_Namespace;
  File f = InternalFS.open(BSEC_STATE_FILE, FILE_O_READ);
  if (!f) return;
  uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
  f.read(state, BSEC_MAX_STATE_BLOB_SIZE);
  f.close();
  bsec_iaq.setState(state);
}

static void bsec_save_state() {
  using namespace Adafruit_LittleFS_Namespace;
  uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
  bsec_iaq.getState(state);
  InternalFS.remove(BSEC_STATE_FILE);
  File f = InternalFS.open(BSEC_STATE_FILE, FILE_O_WRITE);
  if (!f) return;
  f.write(state, BSEC_MAX_STATE_BLOB_SIZE);
  f.close();
}

static uint8_t init_bme680_bsec(TwoWire* wire, uint8_t addr) {
  bsec_iaq.begin(addr, *wire);
  if (bsec_iaq.bsecStatus != BSEC_OK) return 0;

  bsec_iaq.setConfig(bsec_config_iaq);
  if (bsec_iaq.bsecStatus != BSEC_OK) return 0;

  bsec_virtual_sensor_t outputs[] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_STABILIZATION_STATUS,
    BSEC_OUTPUT_RUN_IN_STATUS,
  };
  bsec_iaq.updateSubscription(outputs, 6, BSEC_SAMPLE_RATE_LP);
  if (bsec_iaq.bsecStatus != BSEC_OK) return 0;

  bsec_load_state();
  bsec_active = true;
  return 1;
}

static void query_bme680_bsec(uint8_t ch, uint8_t, CayenneLPP& lpp) {
  if (!bsec_data_ready) return;
  lpp.addTemperature(ch, bsec_temperature);
  lpp.addRelativeHumidity(ch, bsec_humidity);
  lpp.addBarometricPressure(ch, bsec_pressure_hpa);
  lpp.addAltitude(ch, 44330.0f * (1.0f - powf(bsec_pressure_hpa / (float)TELEM_BME680_SEALEVELPRESSURE_HPA, 0.1903f)));
  lpp.addGenericSensor(ch, (uint16_t)bsec_iaq_val);
  lpp.addAnalogInput(ch, (float)bsec_accuracy);
}
#endif

// ============================================================
// Sensor descriptor table
//
// Each entry maps an I2C address to a sensor's init and query
// functions. Only entries whose ENV_INCLUDE_* guard is defined
// are compiled in. The sentinel at the end keeps the array
// non-empty regardless of which sensors are enabled.
//
// Bosch BMP/BME SDO selects 0x76 or 0x77; probe both.
//
// Ordering here determines channel assignment at runtime:
// the first detected+initialized sensor gets channel 2, the
// next gets channel 3, and so on.
// ============================================================

struct SensorDef {
  uint8_t     address;
  const char* name;
  uint8_t   (*init)(TwoWire* wire, uint8_t address);
  void      (*query)(uint8_t channel, uint8_t sub_channel, CayenneLPP& telemetry);
};

#define TELEM_BOSCH_ALT_ADDR(addr) ((uint8_t)((addr) == 0x76 ? 0x77 : 0x76))

static const SensorDef SENSOR_TABLE[] = {
#if ENV_INCLUDE_AHTX0
  { TELEM_AHTX_ADDRESS,    "AHT10/AHT20", init_ahtx0,    query_ahtx0    },
#endif
#ifdef ENV_INCLUDE_BME680
  { TELEM_BME680_ADDRESS,                       "BME680",       init_bme680,      query_bme680      },
  { TELEM_BOSCH_ALT_ADDR(TELEM_BME680_ADDRESS), "BME680",       init_bme680,      query_bme680      },
#endif
#if ENV_INCLUDE_BME680_BSEC
  { TELEM_BME680_ADDRESS,                       "BME680+BSEC",  init_bme680_bsec, query_bme680_bsec },
  { TELEM_BOSCH_ALT_ADDR(TELEM_BME680_ADDRESS), "BME680+BSEC",  init_bme680_bsec, query_bme680_bsec },
#endif
#if ENV_INCLUDE_BME280
  { TELEM_BME280_ADDRESS,                       "BME280",       init_bme280,      query_bme280      },
  { TELEM_BOSCH_ALT_ADDR(TELEM_BME280_ADDRESS), "BME280",       init_bme280,      query_bme280      },
#endif
#if ENV_INCLUDE_BMP280
  { TELEM_BMP280_ADDRESS,                       "BMP280",       init_bmp280,      query_bmp280      },
  { TELEM_BOSCH_ALT_ADDR(TELEM_BMP280_ADDRESS), "BMP280",       init_bmp280,      query_bmp280      },
#endif
#if ENV_INCLUDE_SHTC3
  { 0x70,                  "SHTC3",        init_shtc3,    query_shtc3    },
#endif
#if ENV_INCLUDE_SHT4X
  { TELEM_SHT4X_ADDRESS,   "SHT4X",        init_sht4x,    query_sht4x    },
#endif
#if ENV_INCLUDE_LPS22HB
  { 0x5C,                  "LPS22HB",      init_lps22hb,  query_lps22hb  },
#endif
#if ENV_INCLUDE_INA3221
  { TELEM_INA3221_ADDRESS, "INA3221",      init_ina3221,  query_ina3221  },
#endif
#if ENV_INCLUDE_INA219
  { TELEM_INA219_ADDRESS,  "INA219",       init_ina219,   query_ina219   },
#endif
#if ENV_INCLUDE_INA260
  { TELEM_INA260_ADDRESS,  "INA260",       init_ina260,   query_ina260   },
#endif
#if ENV_INCLUDE_INA226
  { TELEM_INA226_ADDRESS,  "INA226",       init_ina226,   query_ina226   },
#endif
#if ENV_INCLUDE_MLX90614
  { TELEM_MLX90614_ADDRESS,"MLX90614",     init_mlx90614, query_mlx90614 },
#endif
#if ENV_INCLUDE_VL53L0X
  { TELEM_VL53L0X_ADDRESS, "VL53L0X",      init_vl53l0x,  query_vl53l0x  },
#endif
#ifdef ENV_INCLUDE_BMP085
  { 0x77,                  "BMP085",       init_bmp085,   query_bmp085   },
#endif
#if ENV_INCLUDE_RAK12035
  { TELEM_RAK12035_ADDRESS,"RAK12035",     init_rak12035, query_rak12035 },
#endif
  { 0, nullptr, nullptr, nullptr }  // sentinel — keeps the array non-empty
};

#undef TELEM_BOSCH_ALT_ADDR

static const size_t SENSOR_TABLE_SIZE = (sizeof(SENSOR_TABLE) / sizeof(SENSOR_TABLE[0])) - 1;

// ============================================================
// begin() — scan the I2C bus, then initialize only what was
// found. A sensor whose address does not ACK during the scan
// is never touched by a library call, preventing hangs or
// crashes caused by absent or misbehaving hardware.
// ============================================================

bool EnvironmentSensorManager::begin() {
  #if ENV_INCLUDE_GPS
  #ifdef RAK_WISBLOCK_GPS
  rakGPSInit();
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

  // Scan the I2C bus before touching any sensor library.
  bool detected[128] = {};
  scanI2CBus(TELEM_WIRE, detected);

  // Walk the sensor table and initialize only detected devices.
  _active_sensor_count = 0;
  for (size_t i = 0; i < SENSOR_TABLE_SIZE && _active_sensor_count < MAX_ACTIVE_SENSORS; i++) {
    const SensorDef& def = SENSOR_TABLE[i];
    // One static driver instance per type: an alternate address is a fallback, not a second device.
    bool already_active = false;
    for (int j = 0; j < _active_sensor_count; j++) {
      if (_active_sensors[j].query == def.query) { already_active = true; break; }
    }
    if (already_active) continue;
    if (!detected[def.address]) {
      MESH_DEBUG_PRINTLN("%s not detected at I2C address %02X", def.name, def.address);
      continue;
    }
    uint8_t n = def.init(TELEM_WIRE, def.address);
    if (n == 0) {
      MESH_DEBUG_PRINTLN("%s found at %02X but failed to initialize", def.name, def.address);
      continue;
    }
    MESH_DEBUG_PRINTLN("Found %s at address: %02X", def.name, def.address);
    detected[def.address] = false;  // consumed; later entries must not re-claim this device
    for (uint8_t sub = 0; sub < n && _active_sensor_count < MAX_ACTIVE_SENSORS; sub++) {
      _active_sensors[_active_sensor_count++] = { def.query, sub };
    }
  }

  return true;
}

// ============================================================
// querySensors() — GPS stays on channel 1; each active sensor
// gets the next available channel in the order it was
// initialized.
// ============================================================

bool EnvironmentSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  next_available_channel = TELEM_CHANNEL_SELF + 1;

  bool share_location = gps_active;
  #if ENV_INCLUDE_GPS && defined(GPS_GUARD)
  share_location = share_location && _have_trusted_fix;   // never report an unverified position
  #endif
  if (requester_permissions & TELEM_PERM_LOCATION && share_location) {
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }

  if (requester_permissions & TELEM_PERM_ENVIRONMENT) {
    for (int i = 0; i < _active_sensor_count; i++) {
      _active_sensors[i].query(next_available_channel, _active_sensors[i].sub_channel, telemetry);
      next_available_channel++;
    }
    #if ENV_INCLUDE_GPS && defined(GPS_GUARD)
    if (_time_alert) {   // GPS-vs-clock offset in hours on its own channel while the alert is active
      telemetry.addAnalogInput(next_available_channel++, _time_disagree_offset / 3600.0f);
    }
    #endif
  }

  return true;
}

#if ENV_INCLUDE_GPS
#ifdef GPS_GUARD
static uint32_t gpsMinEpoch();
static uint32_t gpsMaxEpoch();
#endif

static const char* const GPS_SETTING_NAMES[] = {
  "gps",
#ifdef GPS_GUARD
  "gps_fence",    // metres from gps_ref, 0 = off
  "gps_ref",      // reference position "lat;lon", 0;0 = none
  "gps_guard",    // read-only status
#endif
};
static const int GPS_SETTING_COUNT = sizeof(GPS_SETTING_NAMES) / sizeof(GPS_SETTING_NAMES[0]);
#endif

int EnvironmentSensorManager::getNumSettings() const {
  int settings = 0;
  #if ENV_INCLUDE_GPS
    if (gps_detected) settings += GPS_SETTING_COUNT;  // only show GPS settings if GPS is detected
  #endif
  return settings;
}

const char* EnvironmentSensorManager::getSettingName(int i) const {
  #if ENV_INCLUDE_GPS
    if (gps_detected && i >= 0 && i < GPS_SETTING_COUNT) {
      return GPS_SETTING_NAMES[i];
    }
  #endif
  return NULL;
}

const char* EnvironmentSensorManager::getSettingValue(int i) const {
  #if ENV_INCLUDE_GPS
    const char* name = getSettingName(i);
    if (name == NULL) return NULL;
    if (strcmp(name, "gps") == 0) {
      return gps_active ? "1" : "0";
    }
    #ifdef GPS_GUARD
    if (strcmp(name, "gps_fence") == 0) {
      snprintf(_setting_buf, sizeof(_setting_buf), "%lu", (unsigned long)_gps_fence_m);
      return _setting_buf;
    }
    if (strcmp(name, "gps_ref") == 0) {
      // ";" not ",": companion custom vars are sent as "name:value,name:value"
      snprintf(_setting_buf, sizeof(_setting_buf), "%.5f;%.5f", _gps_ref_lat, _gps_ref_lon);
      return _setting_buf;
    }
    if (strcmp(name, "gps_guard") == 0) {
      if (_time_alert) {
        snprintf(_setting_buf, sizeof(_setting_buf), "%s time %s ALERT gps-clk %+lds %lum", _fix_status, _time_status,
                 _time_disagree_offset, (unsigned long)((millis() - _time_disagree_since_ms) / 60000));
      } else {
        snprintf(_setting_buf, sizeof(_setting_buf), "%s time %s", _fix_status, _time_status);
      }
      return _setting_buf;
    }
    #endif
  #endif
  return NULL;
}

bool EnvironmentSensorManager::setSettingValue(const char* name, const char* value) {
  #if ENV_INCLUDE_GPS
  if (gps_detected && strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
  if (strcmp(name, "gps_interval") == 0) {
    uint32_t interval_seconds = atoi(value);
    gps_update_interval_sec = interval_seconds > 0 ? interval_seconds : 1;
    return true;
  }
  #ifdef GPS_GUARD
  if (strcmp(name, "gps_fence") == 0) {
    _gps_fence_m = atoi(value);
    return true;
  }
  if (strcmp(name, "gps_ref") == 0) {
    char* end;
    double lat = strtod(value, &end);
    if (*end != ',' && *end != ';') return false;   // accept "lat,lon" and "lat;lon"
    double lon = strtod(end + 1, NULL);
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
    _gps_ref_lat = lat;
    _gps_ref_lon = lon;
    return true;
  }
  if (strcmp(name, "gps_time_reset") == 0) {   // clock was set manually: drop the GPS drift anchor
    _time_accepted = false;
    _time_disagree = false;
    _time_alert = false;
    return true;
  }
  if (strcmp(name, "gps_time_accept") == 0) {  // operator confirms the alerted GPS time is right: apply it now
    if (!_time_alert) return false;            // only resolves an active alert
    if (!_have_trusted_fix || (millis() - _last_trusted_ms) > 5000) return false;
    long offset;
    if (!_location->getClockOffset(offset)) return false;
    if (labs(offset - _time_disagree_offset) > GPS_GUARD_ALERT_TOLERANCE) return false;  // not the alerted offset
    uint32_t gps_time = _location->getTimestamp();
    if (gps_time < gpsMinEpoch() || gps_time > gpsMaxEpoch()) return false;
    if (!_location->applyTimeNow()) return false;
    _time_accepted = true;
    _last_time_accept_ms = millis();
    _time_disagree = false;
    _time_alert = false;
    snprintf(_time_status, sizeof(_time_status), "ok manual");
    return true;
  }
  #endif
  #endif
  return false;  // not supported
}

#if ENV_INCLUDE_GPS
void EnvironmentSensorManager::initBasicGPS() {

  #ifdef GPS_GUARD
  _location->setTimeGuard(timeGuardCallback, this);
  #endif

  Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);

  #ifdef GPS_BAUD_RATE
  Serial1.begin(GPS_BAUD_RATE);
  #else
  Serial1.begin(9600);
  #endif

  // Try to detect if GPS is physically connected to determine if we should expose the setting
  _location->begin();
  _location->reset();

  #ifndef PIN_GPS_EN
    MESH_DEBUG_PRINTLN("No GPS wake/reset pin found for this board. Continuing on...");
  #endif

  // Give the GPS time to power up and emit its first NMEA sentence. Cold-start
  // modules can take several seconds, so poll for up to ~5s instead of checking
  // once after a fixed 1s delay -- returning as soon as any data appears, so
  // fast modules add no boot delay (issue #2258).
#ifdef ENV_SKIP_GPS_DETECT
  gps_detected = true;
#else
  gps_detected = false;
  for (int i = 0; i < 20 && !gps_detected; i++) {
    delay(250);
    gps_detected = (Serial1.available() > 0);
  }
#endif

  if (gps_detected) {
    MESH_DEBUG_PRINTLN("GPS detected");
    #ifdef PERSISTANT_GPS
      gps_active = true;
      return;
    #endif
  } else {
    MESH_DEBUG_PRINTLN("No GPS detected");
  }
  _location->stop();
  gps_active = false; //Set GPS visibility off until setting is changed
}

// gps code for rak might be moved to MicroNMEALoactionProvider
// or make a new location provider ...
#ifdef RAK_WISBLOCK_GPS
void EnvironmentSensorManager::rakGPSInit(){

  Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);

  #ifdef GPS_BAUD_RATE
  Serial1.begin(GPS_BAUD_RATE);
  #else
  Serial1.begin(9600);
  #endif

  //search for the correct IO standby pin depending on socket used
  if(gpsIsAwake(WB_IO2)){
  }
  else if(gpsIsAwake(WB_IO4)){
  }
  else if(gpsIsAwake(WB_IO5)){
  }
  else{
    MESH_DEBUG_PRINTLN("No GPS found");
    gps_active = false;
    gps_detected = false;
    Serial1.end();
    return;
  }

  #ifndef FORCE_GPS_ALIVE // for use with repeaters, until GPS toggle is implimented
  //Now that GPS is found and set up, set to sleep for initial state
  stop_gps();
  #endif
}

bool EnvironmentSensorManager::gpsIsAwake(uint8_t ioPin){

  #if defined(ETHERNET_ENABLED) && defined(RAK_BOARD)
    if (ioPin == WB_IO2) {
      // WB_IO2 powers the Ethernet module on RAK baseboards.
      return false;
    }
  #endif

  #ifndef RAK_3401
  //set initial waking state
  pinMode(ioPin,OUTPUT);
  digitalWrite(ioPin,LOW);
  delay(500);
  digitalWrite(ioPin,HIGH);
  #endif

  // give gps time to power up
  delay(500);

  //Try to init RAK12500 on I2C
  if (ublox_GNSS.begin(Wire) == true){
    MESH_DEBUG_PRINTLN("RAK12500 GPS init correctly with pin %i",ioPin);
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
  } else if (Serial1.available()) {
    MESH_DEBUG_PRINTLN("Serial GPS init correctly and is turned on");
#ifdef PIN_GPS_EN
    if(PIN_GPS_EN){
      gpsResetPin = PIN_GPS_EN;
    }
#endif
    serialGPSFlag = true;
    gps_active = true;
    gps_detected = true;
    return true;
  }

  #ifndef RAK_3401
  pinMode(ioPin, INPUT);
  #endif
  MESH_DEBUG_PRINTLN("GPS did not init with this IO pin... try the next");
  return false;
}
#endif

#ifdef GPS_GUARD
// Plausible GPS time window: not before this firmware was built, at most 10 years after.
static uint32_t gpsMinEpoch() {
  static const uint32_t min_epoch = DateTime(__DATE__, __TIME__).unixtime() - 86400;
  return min_epoch;
}
static uint32_t gpsMaxEpoch() { return gpsMinEpoch() + 10UL * 365 * 86400; }

static double gpsDistanceM(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0, D2R = 0.017453292519943295;
  double dlat = (lat2 - lat1) * D2R, dlon = (lon2 - lon1) * D2R;
  double a = sin(dlat / 2) * sin(dlat / 2) +
             cos(lat1 * D2R) * cos(lat2 * D2R) * sin(dlon / 2) * sin(dlon / 2);
  return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}

// Called once per second while the GPS is powered: decides whether the current fix is trustworthy.
void EnvironmentSensorManager::evaluateGpsFix() {
  if (!_location->isValid()) {
    _stable_count = 0;
    snprintf(_fix_status, sizeof(_fix_status), "no fix %ldsat", _location->satellitesCount());
    return;
  }
  double lat = ((double)_location->getLatitude()) / 1000000.;
  double lon = ((double)_location->getLongitude()) / 1000000.;
  long sats = _location->satellitesCount();
  uint8_t hdop = _location->getHDOP();
  unsigned long now = millis();

  const char* why = NULL;
  double dist = 0;
  if (sats < GPS_GUARD_MIN_SATS) {
    why = "sats";
  } else if (hdop == 255 || hdop > GPS_GUARD_MAX_HDOP10) {
    why = "hdop";
  }
  #if defined(GPS_GUARD_LAT_MIN) && defined(GPS_GUARD_LAT_MAX) && defined(GPS_GUARD_LON_MIN) && defined(GPS_GUARD_LON_MAX)
  else if (lat < GPS_GUARD_LAT_MIN || lat > GPS_GUARD_LAT_MAX || lon < GPS_GUARD_LON_MIN || lon > GPS_GUARD_LON_MAX) {
    why = "region";
  }
  #endif
  else if (_gps_fence_m > 0 && (_gps_ref_lat != 0 || _gps_ref_lon != 0) &&
           (dist = gpsDistanceM(_gps_ref_lat, _gps_ref_lon, lat, lon)) > _gps_fence_m) {
    why = "fence";
  } else if (_have_trusted_fix) {
    dist = gpsDistanceM(_last_trusted_lat, _last_trusted_lon, lat, lon);
    double secs = (now - _last_trusted_ms) / 1000.0;
    if (secs < 1) secs = 1;
    if (dist / secs > GPS_GUARD_MAX_SPEED_KMH / 3.6) why = "jump";
  }

  if (why) {
    _stable_count = 0;
    if (dist > 0) {
      snprintf(_fix_status, sizeof(_fix_status), "rej %s %.1fkm", why, dist / 1000.0);
    } else {
      snprintf(_fix_status, sizeof(_fix_status), "rej %s", why);
    }
    MESH_DEBUG_PRINTLN("GPS fix rejected (%s): %f,%f sats=%ld hdop=%u", why, lat, lon, sats, hdop);
    return;
  }

  // Require several consecutive, mutually consistent fixes.
  if (_stable_count > 0 && gpsDistanceM(_cand_lat, _cand_lon, lat, lon) > GPS_GUARD_STABLE_M) {
    _stable_count = 0;
  }
  _cand_lat = lat;
  _cand_lon = lon;
  if (_stable_count < 255) _stable_count++;
  if (_stable_count < GPS_GUARD_STABLE_FIXES) {
    snprintf(_fix_status, sizeof(_fix_status), "settle %u/%u", _stable_count, GPS_GUARD_STABLE_FIXES);
    return;
  }

  node_lat = lat;
  node_lon = lon;
  node_altitude = ((double)_location->getAltitude()) / 1000.0;
  if (!_have_trusted_fix) {
    MESH_DEBUG_PRINTLN("GPS fix trusted: %f,%f sats=%ld hdop=%u", lat, lon, sats, hdop);
  }
  _have_trusted_fix = true;
  _last_trusted_ms = now;
  _last_trusted_lat = lat;
  _last_trusted_lon = lon;
  snprintf(_fix_status, sizeof(_fix_status), "ok %ldsat", sats);
}

// A trusted fix whose time keeps disagreeing with the clock by the same offset is either
// a sustained spoof or a wrong clock; either way the operator should look at it.
void EnvironmentSensorManager::trackTimeDisagreement(long offset) {
  unsigned long now = millis();
  if (!_time_disagree || labs(offset - _time_disagree_offset) > GPS_GUARD_ALERT_TOLERANCE) {
    _time_disagree = true;
    _time_disagree_offset = offset;
    _time_disagree_since_ms = now;
    _time_alert = false;
    return;
  }
  if (!_time_alert && (now - _time_disagree_since_ms) >= GPS_GUARD_ALERT_MIN * 60000UL) {
    _time_alert = true;
    MESH_DEBUG_PRINTLN("GPS TIME ALERT: GPS differs from clock by %lds for %u min", offset, GPS_GUARD_ALERT_MIN);
  }
}

bool EnvironmentSensorManager::timeGuardCallback(void* ctx, uint32_t gps_time, uint32_t clock_time) {
  return ((EnvironmentSensorManager*)ctx)->checkGpsTime(gps_time, clock_time);
}

// GPS time is only written to the clock while a trusted fix is current, it is not
// older than this firmware, and (once the clock is valid) it only nudges the clock.
bool EnvironmentSensorManager::checkGpsTime(uint32_t gps_time, uint32_t clock_time) {
  const uint32_t min_epoch = gpsMinEpoch();
  const uint32_t max_epoch = gpsMaxEpoch();

  const char* why = NULL;
  long step = 0;
  if (!_have_trusted_fix || (millis() - _last_trusted_ms) > 5000) {
    why = "nofix";
  } else if (gps_time < min_epoch || gps_time > max_epoch) {
    why = "range";
  } else if (clock_time >= min_epoch) {   // clock already holds a plausible time
    step = (long)gps_time - (long)clock_time;
    long limit = GPS_GUARD_TIME_MAX_STEP;
    if (_time_accepted) {
      // After the first accepted sync only allow corrections the size of real clock
      // drift, so a spoofer cannot walk the clock away in many small steps.
      uint32_t elapsed_s = (millis() - _last_time_accept_ms) / 1000;
      limit = GPS_GUARD_TIME_DRIFT_BASE + (long)((uint64_t)elapsed_s * GPS_GUARD_TIME_DRIFT_PPM / 1000000UL);
      if (limit > GPS_GUARD_TIME_MAX_STEP) limit = GPS_GUARD_TIME_MAX_STEP;
    }
    if (labs(step) > limit) why = "step";
  }

  if (why) {
    if (step != 0) {
      snprintf(_time_status, sizeof(_time_status), "rej %s %lds", why, step);
      trackTimeDisagreement(step);
    } else {
      snprintf(_time_status, sizeof(_time_status), "rej %s", why);
    }
    return false;
  }
  snprintf(_time_status, sizeof(_time_status), "ok %+lds", (long)gps_time - (long)clock_time);
  _time_disagree = false;
  _time_alert = false;
  // Every accepted sync anchors the drift budget (fail closed: no walking the clock in
  // repeated MAX_STEP jumps). A wrongly anchored time is recovered with a manual clock
  // set, which resets the anchor (gps_time_reset).
  _time_accepted = true;
  _last_time_accept_ms = millis();
  return true;
}
#endif

void EnvironmentSensorManager::start_gps() {
  gps_active = true;
  #ifdef RAK_WISBLOCK_GPS
    #ifndef RAK_3401
    pinMode(gpsResetPin, OUTPUT);
    digitalWrite(gpsResetPin, HIGH);
    #endif
    return;
  #endif

  _location->begin();
  _location->reset();

#ifndef PIN_GPS_EN
  MESH_DEBUG_PRINTLN("Start GPS is N/A on this board. Actual GPS state unchanged");
#endif
}

void EnvironmentSensorManager::stop_gps() {
  gps_active = false;
  #ifdef RAK_WISBLOCK_GPS
    #ifndef RAK_3401 // rak3401 shouldn't turn off WB_IO2 as it powers the PA
    pinMode(gpsResetPin, OUTPUT);
    digitalWrite(gpsResetPin, LOW);
    #endif
    return;
  #endif

  _location->stop();

  #ifndef PIN_GPS_EN
  MESH_DEBUG_PRINTLN("Stop GPS is N/A on this board. Actual GPS state unchanged");
  #endif
}
#endif // ENV_INCLUDE_GPS

#if ENV_INCLUDE_GPS || defined(ENV_INCLUDE_BME680_BSEC)
void EnvironmentSensorManager::loop() {

  #if ENV_INCLUDE_GPS
  static unsigned long next_gps_update = 0;
  if (gps_active) {
    _location->loop();
  }
  if ((long)(millis() - next_gps_update) > 0) {

    if(gps_active){
    #ifdef RAK_WISBLOCK_GPS
    if ((i2cGPSFlag || serialGPSFlag) && _location->isValid()) {
      node_lat = ((double)_location->getLatitude())/1000000.;
      node_lon = ((double)_location->getLongitude())/1000000.;
      MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      MESH_DEBUG_PRINTLN("lat %f lon %f alt %f", node_lat, node_lon, node_altitude);
    }
    #elif defined(GPS_GUARD)
    // position is updated by evaluateGpsFix() below, only from trusted fixes
    #else
    if (_location->isValid()) {
      node_lat = ((double)_location->getLatitude())/1000000.;
      node_lon = ((double)_location->getLongitude())/1000000.;
      MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      MESH_DEBUG_PRINTLN("lat %f lon %f alt %f", node_lat, node_lon, node_altitude);
    }
    #endif
    }
    next_gps_update = millis() + (gps_update_interval_sec * 1000);
  }

  #if defined(GPS_GUARD) && !defined(RAK_WISBLOCK_GPS)
  static unsigned long next_guard_check = 0;
  if (gps_active && _location->isEnabled() && (long)(millis() - next_guard_check) > 0) {
    next_guard_check = millis() + 1000;
    evaluateGpsFix();
  }
  #endif
  #endif
  #if ENV_INCLUDE_BME680_BSEC
  if (bsec_active && bsec_iaq.run()) {
    uint8_t prev_accuracy = bsec_accuracy;
    bsec_temperature  = bsec_iaq.temperature;
    bsec_humidity     = bsec_iaq.humidity;
    bsec_pressure_hpa = bsec_iaq.pressure / 100.0f;
    bsec_iaq_val      = bsec_iaq.iaq;
    bsec_accuracy     = bsec_iaq.iaqAccuracy;
    bsec_data_ready   = true;

    if (bsec_accuracy == 3) {
      if (!bsec_first_save_done) {
        bsec_save_state();
        bsec_last_save_ms = millis();
        bsec_first_save_done = true;
      } else if ((millis() - bsec_last_save_ms) >= BSEC_SAVE_INTERVAL_MS) {
        bsec_save_state();
        bsec_last_save_ms = millis();
      }
    }
  }
  #endif  // ENV_INCLUDE_BME680_BSEC
}
#endif // ENV_INCLUDE_GPS || ENV_INCLUDE_BME680_BSEC
