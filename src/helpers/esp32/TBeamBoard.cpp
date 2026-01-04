#if defined(TBEAM_SUPREME_SX1262) || defined(TBEAM_SX1262) || defined(TBEAM_SX1276)

#include <Arduino.h>
#include "TBeamBoard.h"
//#include <RadioLib.h>

uint32_t deviceOnline = 0x00;

bool pmuInterrupt;
static void setPmuFlag()
{
  pmuInterrupt = true;
}

void TBeamBoard::begin() {
    
    ESP32Board::begin();

    power_init();

    //Configure user button
    pinMode(PIN_USER_BTN, INPUT);

    #ifndef TBEAM_SUPREME_SX1262
      digitalWrite(P_LORA_TX_LED, HIGH); //inverted pin for SX1276 - HIGH for off
    #endif

    //radiotype_detect();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }
}

#ifdef MESH_DEBUG
void TBeamBoard::scanDevices(TwoWire *w)
{
    uint8_t err, addr;
    int nDevices = 0;
    uint32_t start = 0;

    Serial.println("Scanning I2C for Devices");
    for (addr = 1; addr < 127; addr++) {
        start = millis();
        w->beginTransmission(addr); delay(2);
        err = w->endTransmission();
        if (err == 0) {
            nDevices++;
            switch (addr) {
            case 0x77:
            case 0x76:
                Serial.println("\tFound BME280 Sensor");
                deviceOnline |= BME280_ONLINE;
                break;
            case 0x34:
                Serial.println("\tFound AXP192/AXP2101 PMU");
                deviceOnline |= POWERMANAGE_ONLINE;
                break;
            case 0x3C:
                Serial.println("\tFound SSD1306/SH1106 display");
                deviceOnline |= DISPLAY_ONLINE;
                break;
            case 0x51:
                Serial.println("\tFound PCF8563 RTC");
                deviceOnline |= PCF8563_ONLINE;
                break;
            case 0x1C:
                Serial.println("\tFound QMC6310 MAG Sensor");
                deviceOnline |= QMC6310_ONLINE;
                break;
            default:
                Serial.print("\tI2C device found at address 0x");
                if (addr < 16) {
                    Serial.print("0");
                }
                Serial.print(addr, HEX);
                Serial.println(" !");
                break;
            }

        } else if (err == 4) {
            Serial.print("Unknow error at address 0x");
            if (addr < 16) {
                Serial.print("0");
            }
            Serial.println(addr, HEX);
        }
    }
    if (nDevices == 0)
        Serial.println("No I2C devices found\n");

    Serial.println("Scan for devices is complete.");
    Serial.println("\n");

    Serial.printf("GPS RX pin: %d", PIN_GPS_RX);
    Serial.printf(" GPS TX pin: %d", PIN_GPS_TX);
    Serial.println();
}
void TBeamBoard::printPMU()
{
    Serial.print("isCharging:"); Serial.println(PMU->isCharging() ? "YES" : "NO");
    Serial.print("isDischarge:"); Serial.println(PMU->isDischarge() ? "YES" : "NO");
    Serial.print("isVbusIn:"); Serial.println(PMU->isVbusIn() ? "YES" : "NO");
    Serial.print("getBattVoltage:"); Serial.print(PMU->getBattVoltage()); Serial.println("mV");
    Serial.print("getVbusVoltage:"); Serial.print(PMU->getVbusVoltage()); Serial.println("mV");
    Serial.print("getSystemVoltage:"); Serial.print(PMU->getSystemVoltage()); Serial.println("mV");

    // The battery percentage may be inaccurate at first use, the PMU will automatically
    // learn the battery curve and will automatically calibrate the battery percentage
    // after a charge and discharge cycle
    if (PMU->isBatteryConnect()) {
        Serial.print("getBatteryPercent:"); Serial.print(PMU->getBatteryPercent()); Serial.println("%");
    }

    Serial.println();
}
#endif

bool TBeamBoard::power_init()
{
  if (!PMU) {
    #ifdef TBEAM_SUPREME_SX1262
      PMU = new XPowersAXP2101(PMU_WIRE_PORT, PIN_BOARD_SDA1, PIN_BOARD_SCL1, I2C_PMU_ADD);
    #else
      PMU = new XPowersAXP2101(PMU_WIRE_PORT, PIN_BOARD_SDA, PIN_BOARD_SCL, I2C_PMU_ADD);
    #endif
    if (!PMU || !PMU->init()) {
        MESH_DEBUG_PRINTLN("Warning: Failed to find AXP2101 power management");
        delete PMU;
        PMU = NULL;
    } else {
        MESH_DEBUG_PRINTLN("AXP2101 PMU init succeeded, using AXP2101 PMU");
    }
  }
  if (!PMU) {
    PMU = new XPowersAXP192(PMU_WIRE_PORT, PIN_BOARD_SDA, PIN_BOARD_SCL, I2C_PMU_ADD);
     if (!PMU || !PMU->init()) {
        MESH_DEBUG_PRINTLN("Warning: Failed to find AXP192 power management");
        delete PMU;
        PMU = NULL;
    } else {
        MESH_DEBUG_PRINTLN("AXP192 PMU init succeeded, using AXP192 PMU");
    }
  }

  if (!PMU) {
    return false;
  }

  deviceOnline |= POWERMANAGE_ONLINE;

  PMU->setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);

  // Set up PMU interrupts
  pinMode(PIN_PMU_IRQ, INPUT_PULLUP);
  attachInterrupt(PIN_PMU_IRQ, setPmuFlag, FALLING);

  if (PMU->getChipModel() == XPOWERS_AXP192) {

    PMU->setPowerChannelVoltage(XPOWERS_LDO2, 3300);  //Set up LoRa power rail
    PMU->enablePowerOutput(XPOWERS_LDO2);             //Enable the LoRa power rail

    PMU->setPowerChannelVoltage(XPOWERS_DCDC1, 3300); //Set up OLED power rail
    PMU->enablePowerOutput(XPOWERS_DCDC1);            //Enable the OLED power rail

    PMU->setPowerChannelVoltage(XPOWERS_LDO3, 3300);  //Set up GPS power rail
    PMU->enablePowerOutput(XPOWERS_LDO3);             //Enable the GPS power rail

    PMU->setProtectedChannel(XPOWERS_DCDC1);          //Protect the OLED power rail
    PMU->setProtectedChannel(XPOWERS_DCDC3);          //Protect the ESP32 power rail

    PMU->disablePowerOutput(XPOWERS_DCDC2);           //Disable unsused power rail DC2

    PMU->disableIRQ(XPOWERS_AXP192_ALL_IRQ);          //Disable PMU IRQ

    PMU->setChargerConstantCurr(XPOWERS_AXP192_CHG_CUR_450MA);  //Set battery charging current
    PMU->setChargeTargetVoltage(XPOWERS_AXP192_CHG_VOL_4V2);    //Set battery charge-stop voltage
  }
  else if(PMU->getChipModel() == XPOWERS_AXP2101){
    #ifdef TBEAM_SUPREME_SX1262
      //Set up the GPS power rail
      PMU->setPowerChannelVoltage(XPOWERS_ALDO4, 3300);
      PMU->enablePowerOutput(XPOWERS_ALDO4);

      //Set up the LoRa power rail
      PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);
      PMU->enablePowerOutput(XPOWERS_ALDO3);

      //Set up power rail for the M.2 interface
      PMU->setPowerChannelVoltage(XPOWERS_DCDC3, 3300);
      PMU->enablePowerOutput(XPOWERS_DCDC3);

      if (ESP_SLEEP_WAKEUP_UNDEFINED == esp_sleep_get_wakeup_cause()) {
        MESH_DEBUG_PRINTLN("Power off and restart ALDO BLDO..");
        PMU->disablePowerOutput(XPOWERS_ALDO1);
        PMU->disablePowerOutput(XPOWERS_ALDO2);
        PMU->disablePowerOutput(XPOWERS_BLDO1);
        delay(250);
      }

      //Set up power rail for QMC6310U
      PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);
      PMU->enablePowerOutput(XPOWERS_ALDO2);

      //Set up power rail for BME280 and OLED
      PMU->setPowerChannelVoltage(XPOWERS_ALDO1, 3300);
      PMU->enablePowerOutput(XPOWERS_ALDO1);

      //Set up pwer rail for SD Card
      PMU->setPowerChannelVoltage(XPOWERS_BLDO1, 3300);
      PMU->enablePowerOutput(XPOWERS_BLDO1);

      //Set up power rail BLDO2 to headers
      PMU->setPowerChannelVoltage(XPOWERS_BLDO2, 3300);
      PMU->enablePowerOutput(XPOWERS_BLDO2);

      //Set up power rail DCDC4 to headers
      PMU->setPowerChannelVoltage(XPOWERS_DCDC4, XPOWERS_AXP2101_DCDC4_VOL2_MAX);
      PMU->enablePowerOutput(XPOWERS_DCDC4);

      //Set up power rail DCDC5 to headers
      PMU->setPowerChannelVoltage(XPOWERS_DCDC5, 3300);
      PMU->enablePowerOutput(XPOWERS_DCDC5);

      //Disable unused power rails
      PMU->disablePowerOutput(XPOWERS_DCDC2);
      PMU->disablePowerOutput(XPOWERS_DLDO1);
      PMU->disablePowerOutput(XPOWERS_DLDO2);
      PMU->disablePowerOutput(XPOWERS_VBACKUP);
    #else
      //Turn off unused power rails
      PMU->disablePowerOutput(XPOWERS_DCDC2);
      PMU->disablePowerOutput(XPOWERS_DCDC3);
      PMU->disablePowerOutput(XPOWERS_DCDC4);
      PMU->disablePowerOutput(XPOWERS_DCDC5);
      PMU->disablePowerOutput(XPOWERS_ALDO1);
      PMU->disablePowerOutput(XPOWERS_ALDO4);
      PMU->disablePowerOutput(XPOWERS_BLDO1);
      PMU->disablePowerOutput(XPOWERS_BLDO2);
      PMU->disablePowerOutput(XPOWERS_DLDO1);
      PMU->disablePowerOutput(XPOWERS_DLDO2);
      //PMU->disablePowerOutput(XPOWERS_CPULDO);

      PMU->setPowerChannelVoltage(XPOWERS_VBACKUP, 3300);   //Set up GPS RTC power
      PMU->enablePowerOutput(XPOWERS_VBACKUP);              //Turn on GPS RTC power

      PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);     //Set up LoRa power rail
      PMU->enablePowerOutput(XPOWERS_ALDO2);                //Enable LoRa power rail

      PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);     //Set up GPS power rail
      PMU->enablePowerOutput(XPOWERS_ALDO3);                //Enable GPS power rail
  
    #endif

    PMU->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);      //Disable all PMU interrupts

    PMU->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA);   //Set battery charging current to 500mA
    PMU->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);     //Set battery charging cutoff voltage to 4.2V

  }

  PMU->clearIrqStatus();        //Clear interrupt flags

  PMU->disableTSPinMeasure();   //Disable TS detection, since it is not used

  //Enable voltage measurements
  PMU->enableSystemVoltageMeasure();
  PMU->enableVbusVoltageMeasure();
  PMU->enableBattVoltageMeasure();

#ifdef MESH_DEBUG
  scanDevices(&Wire);
  printPMU();
#endif

  // Set the power key off press time
  PMU->setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
  return true;
}

#pragma region "Debug code"
// void TBeamBoard::radiotype_detect(){

//   static SPIClass spi;
//   char  chipTypeInfo;

//   #if defined(P_LORA_SCLK)
//     spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
//   #endif

//   for(int i = 0; i<radioVersions; i++){
//     switch(i){
//       case 0: 
//         CustomSX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, spi);
//         int status = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 8);
//         if (status != RADIOLIB_ERR_NONE) {
//           Serial.print("ERROR: SX1262 not found: ");
//           Serial.println(status);
//           //delete radio;
//           radio = NULL;
//           break;
//         }
//         else{
//           MESH_DEBUG_PRINTLN("SX1262 detected");
//           P_LORA_BUSY = 32;
//           RADIO_CLASS = CustomSX1262;
//           WRAPPER_CLASS = CustomSX1262Wrapper;
//           SX126X_RX_BOOSTED_GAIN = true;
//           SX126X_CURRENT_LIMIT = 140;
//           //delete radio;
//           radio = NULL;
//           break;
//         }
//       case 1:
//         SX1276 radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, spi);
//         int status1 = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, LORA_TX_POWER, 8);
//         if (status1 != RADIOLIB_ERR_NONE) {
//           Serial.print("ERROR: SX1272 not found: ");
//           Serial.println(status1);
//           //delete radio;
//           radio = NULL;
//         }
//         else{
//           MESH_DEBUG_PRINTLN("SX1272 detected");
//           P_LORA_BUSY = RADIOLIB_NC;
//           P_LORA_DIO_2 = 32;
//           RADIO_CLASS = CustomSX1272;
//           WRAPPER_CLASS = CustomSX1272Wrapper;
//           SX127X_CURRENT_LIMIT = 120;
//           //delete radio;
//           radio = NULL;
//           return;
//         }
//         default:
//     }
//   }


  
// }
#pragma endregion

#endif
