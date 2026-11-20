#pragma once

#include <helpers/radiolib/CustomLLCC68.h>

class Chatter2Radio : public CustomLLCC68 {
  bool _isLLCC68 = false;

  bool detectChip() {
    Module* module = getMod();
    module->init();
    module->hal->pinMode(module->getIrq(), module->hal->GpioModeInput);
    module->hal->pinMode(module->getGpio(), module->hal->GpioModeInput);
    module->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_ADDR] = Module::BITS_16;
    module->spiConfig.widths[RADIOLIB_MODULE_SPI_WIDTH_CMD] = Module::BITS_8;
    module->spiConfig.statusPos = 1;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_READ] = RADIOLIB_SX126X_CMD_READ_REGISTER;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_WRITE] = RADIOLIB_SX126X_CMD_WRITE_REGISTER;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_NOP] = RADIOLIB_SX126X_CMD_NOP;
    module->spiConfig.cmds[RADIOLIB_MODULE_SPI_COMMAND_STATUS] = RADIOLIB_SX126X_CMD_GET_STATUS;
    module->spiConfig.stream = true;
    module->spiConfig.parseStatusCb = SPIparseStatus;

    if (findChip(RADIOLIB_LLCC68_CHIP_TYPE)) {
      chipType = RADIOLIB_LLCC68_CHIP_TYPE;
      _isLLCC68 = true;
      Serial.println("Detected LLCC68");
      return true;
    }

    if (findChip(RADIOLIB_SX1262_CHIP_TYPE)) {
      chipType = RADIOLIB_SX1262_CHIP_TYPE;
      _isLLCC68 = false;
      Serial.println("Detected SX1262");
      return true;
    }

    Serial.println("ERROR: unsupported or undetected SX126x radio");
    return false;
  }

public:
  using CustomLLCC68::CustomLLCC68;

  bool std_init(SPIClass* spi = nullptr) {
    if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    if (!detectChip()) return false;

    uint8_t cr = 5;
#ifdef LORA_CR
    cr = LORA_CR;
#endif

    float tcxo = 0.0f;
#ifdef CHATTER2_SX1262_TCXO_VOLTAGE
    if (!_isLLCC68) tcxo = CHATTER2_SX1262_TCXO_VOLTAGE;
#endif

    int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr,
                       RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
    if (status != RADIOLIB_ERR_NONE) {
      Serial.print("ERROR: radio init failed: ");
      Serial.println(status);
      return false;
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
  #ifndef SX126X_RXEN
    #define SX126X_RXEN RADIOLIB_NC
  #endif
  #ifndef SX126X_TXEN
    #define SX126X_TXEN RADIOLIB_NC
  #endif
    setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
#endif
    return true;
  }

  bool isLLCC68() const { return _isLLCC68; }
};
