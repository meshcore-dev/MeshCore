#include "target.h"

#include <Arduino.h>
#include <SparkFun_u-blox_GNSS_v3.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

// -----------------------------
// Board info
// -----------------------------
SEIRv5Board board;

// -----------------------------
// Clock info
// -----------------------------
ESP32RTCClock rtc_clock;

// -----------------------------
// GPS info
// -----------------------------
#ifndef GPS_RX_PIN
#define GPS_RX_PIN 5
#endif
#ifndef GPS_TX_PIN
#define GPS_TX_PIN 7
#endif

SFE_UBLOX_GNSS_SERIAL myGNSS;
HardwareSerial gpsSerial(GPS_UART_NUM);
static MicroNMEALocationProvider _gps(gpsSerial, &rtc_clock);
LocationProvider &location = _gps;
SEIRSensorManager sensors(location);

// -----------------------------
// LORA info
// -----------------------------
// Colin: Calibrated freq offset, don't touch!
#ifndef FREQ_OFFSET_MHZ
#define FREQ_OFFSET_MHZ -0.028
#endif
#ifndef LORA_CR
#define LORA_CR 8
#endif
static SPIClass spi(FSPI);
static Module radio_module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

static CustomSX1262 radio(&radio_module);
CustomSX1262Wrapper radio_driver(radio, board);

// -----------------------------
// Init of the device (radio)
// -----------------------------
bool radio_init() {
  // Initialize GPS
  gpsSerial.begin(38400, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // Log GPS provider information
  Serial.printf("[SEIR] GPS provider = %p\n", sensors.getLocationProvider());

  // Attempt to initialize GPS module
  if (!myGNSS.begin(gpsSerial)) {
    Serial.println("u-blox GNSS not responding");
    // Continue with radio initialization since GPS is optional for core functionality
  } else {
    Serial.println("u-blox GNSS responding");

    // Configure GPS module for optimal operation
    // Enable NMEA output on UART1
    if (!myGNSS.setUART1Output(COM_TYPE_NMEA, VAL_LAYER_RAM_BBR)) {
      Serial.println("GPS: Failed to set UART1 output");
    }

    // Set 1 Hz navigation rate
    if (!myGNSS.setNavigationFrequency(1, VAL_LAYER_RAM_BBR)) {
      Serial.println("GPS: Failed to set navigation frequency");
    }

    // Set portable dynamics model for better performance
    if (!myGNSS.setDynamicModel(DYN_MODEL_PORTABLE, VAL_LAYER_RAM_BBR)) {
      Serial.println("GPS: Failed to set dynamic model");
    }

    // Disable UBX AutoPVT (not needed for NMEA)
    if (!myGNSS.setAutoPVT(false)) {
      Serial.println("GPS: Failed to disable AutoPVT");
    }

    // Configure NMEA messages (all messages enabled)
    myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GLL_UART1, 1);
    myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GSA_UART1, 1);
    myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GSV_UART1, 1);
    myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_RMC_UART1, 1);
    myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_VTG_UART1, 1);
    myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_GGA_UART1, 1);
    myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_NMEA_ID_ZDA_UART1, 1);

    // Apply configuration settings
    if (myGNSS.sendCfgValset()) {
      Serial.println("u-blox has been configured!");
    } else {
      Serial.println("u-blox configuration has failed.");
    }
  }

  // Initialize LoRa radio
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  if (radio.std_init(&spi)) {
    radio.setSyncWord(RADIOLIB_SX126X_SYNC_WORD_PRIVATE);
    radio.explicitHeader();
    radio.setCRC(2);
    radio.setIrq(true);
    radio.setPreambleLength(16);
    radio.setRxBoostedGainMode(true);
    return true;
  } else {
    Serial.println("LoRa radio initialization failed");
    return false;
  }
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  float real_freq = freq + FREQ_OFFSET_MHZ;
  radio.setFrequency(real_freq, false);
  radio.setBandwidth(bw);
  radio.setSpreadingFactor(sf);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}
