#pragma once

/* ESP32-S3 Supermini + Waveshare Core1121 (LR1121-HF) + WeAct ePaper 2.9"
 *
 *                    _____[:::::]_____
 *     IN/OUT - 5v - |     []   []     | - ?? - UART-0 Tx
 *             GND - |     B+   B-     | - ?? - UART-0 Rx
 *      OUT - 3.3v - |       ---       | - 01 - LORA_IRQ
 * LORA_BUSY  - 13 - |      BOOST      | - 02 -
 * LORA_RESET - 12 - |                 | - 03 -
 * LORA_MISO  - 11 - |                 | - 04 - I2C_SDA
 * LORA_MOSI  - 10 - |                 | - 05 - I2C_SCL
 * LORA_SCK   - 09 - |                 | - 06 - GPS_Rx / Serial1_Tx
 * LORA_CS    - 08 - |                 | - 07 - GPS_Tx / Serial1_Rx
 *                    *****************
 *
 * Additional pins on board:
 *
 *           34          38         LED - 48   NOTE: Regular LED and WS2818 RGB led IN at same pin!
 *           33          37   EINK_MISO - 47
 *           21          36   EINK_MOSI - 46
 *           18          35   EINK_SCK  - 45
 *           17               EINK_CS   - 42
 * BAT_ADC - 16               EINK_DC   - 41
 *           15               EINK_RST  - 40
 *           14               EINK_BUSY - 39
 *
*/

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/CustomLR1121Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include "SuperminiBoard.h"

// Display
#include <helpers/ui/GxEPDDisplay.h>
#include <helpers/ui/MomentaryButton.h>

extern GxEPDDisplay display;
extern MomentaryButton user_btn;

// Board
extern SuperminiBoard board;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

// Radio / LoRa module
extern CustomLR1121Wrapper radio_driver;
bool radio_init();
mesh::LocalIdentity radio_new_identity();
