#pragma once

#include <RadioLib.h>

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
      ConfigLoRa_t cfg;
      cfg.frequency = LORA_FREQ;
      cfg.bandwidth = LORA_BW;
      cfg.spreadingFactor = LORA_SF;
  #ifdef LORA_CR
      cfg.codingRate = LORA_CR;
  #else
      cfg.codingRate = 5;
  #endif
      cfg.syncWord = RADIOLIB_LORA_SYNC_WORD_PRIVATE;
      cfg.power = LORA_TX_POWER;
      cfg.preambleLength = 16;

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
      int status = begin(cfg);
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false;  // fail
      }
  #ifdef SX127X_CURRENT_LIMIT
      setCurrentLimit(SX127X_CURRENT_LIMIT);
  #endif

  #if defined(SX127X_RXEN) || defined(SX127X_TXEN)
    #ifndef SX127X_RXEN
      #define SX127X_RXEN RADIOLIB_NC
    #endif
    #ifndef SX127X_TXEN
      #define SX127X_TXEN RADIOLIB_NC
    #endif
      setRfSwitchPins(SX127X_RXEN, SX127X_TXEN);
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
