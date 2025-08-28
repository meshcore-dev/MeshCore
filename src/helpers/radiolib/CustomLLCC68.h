#pragma once

#include <RadioLib.h>
#include "Defaults.h"

#define SX126X_IRQ_HEADER_VALID                     0b0000010000  //  4     4     valid LoRa header received
#define SX126X_IRQ_PREAMBLE_DETECTED           0x04

class CustomLLCC68 : public LLCC68 {
  public:
    CustomLLCC68(Module *mod) : LLCC68(mod) { }

  #ifdef RP2040_PLATFORM
    bool std_init(SPIClassRP2040* spi = NULL)
  #else
    bool std_init(SPIClass* spi = NULL)
  #endif
    {
      
  #ifdef SX126X_DIO3_TCXO_VOLTAGE
      float tcxo = SX126X_DIO3_TCXO_VOLTAGE;
  #else
      float tcxo = 1.6f;
  #endif

  #if defined(P_LORA_SCLK)
    #ifdef NRF52_PLATFORM
      if (spi) { spi->setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI); spi->begin(); }
    #elif defined(RP2040_PLATFORM)
      if (spi) {
        spi->setMISO(P_LORA_MISO);
        //spi->setCS(P_LORA_NSS); // Setting CS results in freeze
        spi->setSCK(P_LORA_SCLK);
        spi->setMOSI(P_LORA_MOSI);
        spi->begin();
      }
    #else
      if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    #endif
  #endif
      int status = begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, LORA_PREAMBLE, tcxo);
      // if radio init fails with -707/-706, try again with tcxo voltage set to 0.0f
      if (status == RADIOLIB_ERR_SPI_CMD_FAILED || status == RADIOLIB_ERR_SPI_CMD_INVALID) {
        #define SX126X_DIO3_TCXO_VOLTAGE (0.0f);
        tcxo = SX126X_DIO3_TCXO_VOLTAGE;
        status = begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, LORA_PREAMBLE, tcxo);
      }
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false;  // fail
      }
    
      setCRC(1);
  
  #ifdef SX126X_CURRENT_LIMIT
      setCurrentLimit(SX126X_CURRENT_LIMIT);
  #endif
  #ifdef SX126X_DIO2_AS_RF_SWITCH
      setDio2AsRfSwitch(SX126X_DIO2_AS_RF_SWITCH);
  #endif
  #ifdef SX126X_RX_BOOSTED_GAIN
      setRxBoostedGainMode(SX126X_RX_BOOSTED_GAIN);
  #endif
  #if defined(SX126X_RXEN) || defined(SX126X_TXEN)
    #ifndef SX1262X_RXEN
      #define SX1262X_RXEN RADIOLIB_NC
    #endif
    #ifndef SX1262X_TXEN
      #define SX1262X_TXEN RADIOLIB_NC
    #endif
      setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
  #endif 

    return true;  // success
  }

    bool isReceiving() {
      uint16_t irq = getIrqFlags();
      bool detected = (irq & SX126X_IRQ_HEADER_VALID) || (irq & SX126X_IRQ_PREAMBLE_DETECTED);
      return detected;
    }
};