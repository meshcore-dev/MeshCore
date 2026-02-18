#pragma once

#include <MeshCore.h>
#include <Arduino.h>

// LoRa radio module pins for Heltec T114
#define  P_LORA_DIO_1     (0+9)
#define  P_LORA_NSS       PIN_SPI_NSS
#define  P_LORA_RESET     (0+10)
#define  P_LORA_BUSY      (0+29)
#define  P_LORA_SCLK      PIN_SPI_SCK
#define  P_LORA_MISO      PIN_SPI_MISO
#define  P_LORA_MOSI      PIN_SPI_MOSI
#define  SX126X_POWER_EN  (0+13)

#define SX126X_DIO2_AS_RF_SWITCH  true
#define SX126X_DIO3_TCXO_VOLTAGE   1.8

// built-ins
#define  PIN_VBAT_READ    (0+31)
#define ADC_MULTIPLIER    (2.025F)

//#define  PIN_BAT_CTL      -1
//#define  MV_LSB   (3000.0F / 4096.0F) // 12-bit ADC with 3.0V input range

class RAKkillerBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  uint8_t btn_prev_state;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
  
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    delay(10);
    float volts = (analogRead(BATTERY_PIN) * ADC_MULTIPLIER * AREF_VOLTAGE) / 4096;

    return volts * 1000;
  } 
  
  const char* getManufacturerName() const override {
    return "RAK killer";
  }

int buttonStateChanged() {
    #ifdef BUTTON_PIN
      uint8_t v = digitalRead(BUTTON_PIN);
      if (v != btn_prev_state) {
        btn_prev_state = v;
        return (v == LOW) ? 1 : -1;
      }
    #endif
      return 0;
  }

  void reboot() override {
    NVIC_SystemReset();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
};
