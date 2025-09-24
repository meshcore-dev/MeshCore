#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

// LoRa radio module pins when paired with the RAK13300 SX1262 module
#define  P_LORA_DIO_1   22    // GPIO22 (ESP32 pin 36 -> IO6/DIO1)
#define  P_LORA_NSS     32    // GPIO32 (ESP32 pin 8 -> SPI_CS)
#define  P_LORA_RESET   23    // GPIO23 (ESP32 pin 37 -> IO4/NRESET)
#define  P_LORA_BUSY    13    // GPIO13 (ESP32 pin 16 -> IO5)
#define  P_LORA_SCLK    33    // GPIO33 (ESP32 pin 9 -> SPI_SCK)
#define  P_LORA_MISO    35    // GPIO35 (ESP32 pin 7 -> SPI_MISO)
#define  P_LORA_MOSI    25    // GPIO25 (ESP32 pin 10 -> SPI_MOSI)
#define  SX126X_POWER_EN  27    // GPIO27 (ESP32 pin 12 -> IO2)
#define  PIN_VBAT_READ    36    // WB_A0 for battery reading
#define  ADC_MULTIPLIER   1.8  // Adjusted for ESP32 ADC and voltage divider

class RAK11200Board : public ESP32Board {

public:
  void begin();
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override;

};
