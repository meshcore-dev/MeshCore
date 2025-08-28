#pragma once

#include <RadioLib.h>
#include "Defaults.h"

#define RH_RF95_MODEM_STATUS_CLEAR               0x10
#define RH_RF95_MODEM_STATUS_HEADER_INFO_VALID   0x08
#define RH_RF95_MODEM_STATUS_RX_ONGOING          0x04
#define RH_RF95_MODEM_STATUS_SIGNAL_SYNCHRONIZED 0x02
#define RH_RF95_MODEM_STATUS_SIGNAL_DETECTED     0x01

class CustomSX1276 : public SX1276 {
  public:
    CustomSX1276(Module *mod) : SX1276(mod) { }

  #ifdef RP2040_PLATFORM
    bool std_init(SPIClassRP2040* spi = NULL)
  #else
    bool std_init(SPIClass* spi = NULL)
  #endif
    {

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
      int status = begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, LORA_PREAMBLE);
      // if radio init fails with -707/-706, try again with tcxo voltage set to 0.0f
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false;  // fail
      }
  #ifdef SX127X_CURRENT_LIMIT
      setCurrentLimit(SX127X_CURRENT_LIMIT);
  #endif

  #if defined(SX176X_RXEN) || defined(SX176X_TXEN)
    #ifndef SX176X_RXEN
      #define SX176X_RXEN RADIOLIB_NC
    #endif
    #ifndef SX176X_TXEN
      #define SX176X_TXEN RADIOLIB_NC
    #endif
      setRfSwitchPins(SX176X_RXEN, SX176X_TXEN);
  #endif

      setCRC(1);

      return true;  // success
    }

    bool isReceiving() {
      return (getModemStatus() &
         (RH_RF95_MODEM_STATUS_SIGNAL_DETECTED
        | RH_RF95_MODEM_STATUS_SIGNAL_SYNCHRONIZED
        | RH_RF95_MODEM_STATUS_HEADER_INFO_VALID)) != 0;
    }

    int tryScanChannel() {
      // start CAD
      int16_t state = startChannelScan();
      RADIOLIB_ASSERT(state);

      // wait for channel activity detected or timeout
      unsigned long timeout = millis() + 16;
      while(!this->mod->hal->digitalRead(this->mod->getIrq()) && millis() < timeout) {
        this->mod->hal->yield();
        if(this->mod->hal->digitalRead(this->mod->getGpio())) {
          return(RADIOLIB_PREAMBLE_DETECTED);
        }
      }
      return 0; // timed out
    }
};
