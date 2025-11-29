// from https://github.com/espressif/arduino-esp32/pull/11985/files
#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include "soc/soc_caps.h"

#define USB_VID          0x303A
#define USB_PID          0x1001
#define USB_MANUFACTURER "Arduino"
#define USB_PRODUCT      "Nesso N1"
#define USB_SERIAL       ""

static const uint8_t TX = -1;
static const uint8_t RX = -1;

static const uint8_t SDA = 10;
static const uint8_t SCL = 8;

static const uint8_t MOSI = 21;
static const uint8_t MISO = 22;
static const uint8_t SCK = 20;
static const uint8_t SS = 23;

static const uint8_t D1 = 7;
static const uint8_t D2 = 2;
static const uint8_t D3 = 6;

static const uint8_t IR_TX_PIN = 9;
static const uint8_t BEEP_PIN = 11;

static const uint8_t GROVE_IO_0 = 5;
static const uint8_t GROVE_IO_1 = 4;

static const uint8_t LORA_IRQ = 15;
static const uint8_t LORA_CS = 23;
static const uint8_t LORA_BUSY = 19;

static const uint8_t SYS_IRQ = 3;

static const uint8_t LCD_CS = 17;
static const uint8_t LCD_RS = 16;


// AW32001 registers
static const uint8_t AW32001_REG_PWR_CFG = 0x01; // Power Configuration
static const uint8_t AW32001_REG_CHR_CUR = 0x02; // Charging current
static const uint8_t AW32001_REG_CHR_VOL = 0x04; // Charge voltage
static const uint8_t AW32001_REG_CHR_TMR = 0x05; // Charge timer
static const uint8_t AW32001_REG_SYS_STA = 0x08; // System status
static const uint8_t AW32001_REG_CHIP_ID = 0x0A; // ChipID

static const uint8_t AW32001_I2C_CHIP_ADDR = 0x49; // ChipID



#if !defined(MAIN_ESP32_HAL_GPIO_H_) && defined(__cplusplus)
/* address: 0x43/0x44 */
class ExpanderPin {
public:
  ExpanderPin(uint16_t _pin) : pin(_pin & 0xFF), address(_pin & 0x100 ? 0x44 : 0x43){};
  uint8_t pin;
  uint8_t address;
};

class NessoBattery {
private:
  bool _power_mgmt_init = false;
public:
  enum ChargeStatus
  {
    CS_UNKNOWN = -1,
    CS_NOT_CHARGING = 0,
    CS_PRE_CHARGE = 1,
    CS_CHARGE = 2,
    CS_CHARGE_DONE = 3
  };

  NessoBattery(){};
  void begin();             // setup and check power management chip
  void enableCharge();        // enable charging via power management chip
  float getVoltage();         // get battery voltage in Volts
  uint16_t getMilliVoltage(); // get battery voltage in millivolts
  uint16_t getChargeLevel();  // get battery charge level in percents
  ChargeStatus getChargeStatus();
  bool isCharging();
};

extern ExpanderPin LORA_LNA_ENABLE;
extern ExpanderPin LORA_ANTENNA_SWITCH;
extern ExpanderPin LORA_ENABLE;
extern ExpanderPin POWEROFF;
extern ExpanderPin GROVE_POWER_EN;
extern ExpanderPin VIN_DETECT;
extern ExpanderPin LCD_RESET;
extern ExpanderPin LCD_BACKLIGHT;
extern ExpanderPin LED_BUILTIN;
extern ExpanderPin KEY1;
extern ExpanderPin KEY2;

void pinMode(ExpanderPin pin, uint8_t mode);
void digitalWrite(ExpanderPin pin, uint8_t val);
int digitalRead(ExpanderPin pin);
#endif

#endif /* Pins_Arduino_h */