#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// LoRa radio module pins for RAK13302
#define  P_LORA_SCLK    3
#define  P_LORA_MISO    29
#define  P_LORA_MOSI    30
#define  P_LORA_NSS     26
#define  P_LORA_DIO_1   10
#define  P_LORA_BUSY    9
#define  P_LORA_RESET   4
#ifndef  P_LORA_PA_EN
  #define  P_LORA_PA_EN  31
#endif

//#define PIN_GPS_SDA       13  //GPS SDA pin (output option)
//#define PIN_GPS_SCL       14  //GPS SCL pin (output option)
// #define PIN_GPS_TX        16  //GPS TX pin
// #define PIN_GPS_RX        15  //GPS RX pin
#define PIN_GPS_1PPS      17  //GPS PPS pin
#define GPS_BAUD_RATE   9600
#define GPS_ADDRESS   0x42  //i2c address for GPS

#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE   1.8


// built-ins
#define  PIN_VBAT_READ    5
#define  ADC_MULTIPLIER   (3 * 1.73 * 1.187 * 1000)

#define PIN_3V3_EN (34)
#define WB_IO2 PIN_3V3_EN

class RAK3401Board : public NRF52BoardDCDC, public NRF52BoardOTA {
public:
  RAK3401Board() : NRF52BoardOTA("RAK3401_OTA") {}
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
    return "RAK 3401";
  }

#ifdef P_LORA_PA_EN
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_PA_EN, HIGH);  // Enable PA before transmission
  }

  void onAfterTransmit() override {
    digitalWrite(P_LORA_PA_EN, LOW);   // Disable PA after transmission to save power
  }
#endif
};
