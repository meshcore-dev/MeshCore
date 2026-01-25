#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// LoRa radio module pins for RAK4631
#define  P_LORA_DIO_1   47
#define  P_LORA_NSS     42
#define  P_LORA_RESET  RADIOLIB_NC   // 38
#define  P_LORA_BUSY    46
#define  P_LORA_SCLK    43
#define  P_LORA_MISO    45
#define  P_LORA_MOSI    44
#define  SX126X_POWER_EN  37

//#define PIN_GPS_SDA       13  //GPS SDA pin (output option)
//#define PIN_GPS_SCL       14  //GPS SCL pin (output option)
//#define PIN_GPS_TX        16  //GPS TX pin
//#define PIN_GPS_RX        15  //GPS RX pin
#define PIN_GPS_1PPS      17  //GPS PPS pin
#define GPS_BAUD_RATE   9600
#define GPS_ADDRESS   0x42  //i2c address for GPS
 
#define SX126X_DIO2_AS_RF_SWITCH  true
#define SX126X_DIO3_TCXO_VOLTAGE   1.8

// built-ins
#define  PIN_VBAT_READ    5
#define  ADC_MULTIPLIER   (3 * 1.73 * 1.187 * 1000)

class RAK4631Board : public NRF52BoardDCDC, public NRF52BoardOTA {
protected:
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  RAK4631Board() : NRF52BoardOTA("RAK4631_OTA") {}
  void begin();

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    return (ADC_MULTIPLIER * raw) / 4096;
  }

  const char* getManufacturerName() const override {
    return "RAK 4631";
  }
};
