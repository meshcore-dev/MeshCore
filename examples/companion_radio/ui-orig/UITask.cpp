#include "UITask.h"
#include <Arduino.h>
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"

#define AUTO_OFF_MILLIS     15000   // 15 seconds
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

// 'meshcore', 128x13px
static const uint8_t meshcore_logo [] PROGMEM = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe, 
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc, 
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00, 
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00, 
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0, 
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00, 
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00, 
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8, 
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  clearMsgPreview();
  _node_prefs = node_prefs;
  if (_display != NULL) {
    _display->turnOn();
  }

  // strip off dash and commit hash by changing dash to null terminator
  // e.g: v1.2.3-abcdef -> v1.2.3
  char *version = strdup(FIRMWARE_VERSION);
  char *dash = strchr(version, '-');
  if (dash) {
    *dash = 0;
  }

  // v1.2.3 (1 Jan 2025)
  sprintf(_version_info, "%s (%s)", version, FIRMWARE_BUILD_DATE);

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
#endif

  // Initialize digital button if available
#ifdef PIN_USER_BTN
  _userButton = new Button(PIN_USER_BTN, USER_BTN_PRESSED);
  _userButton->begin();
  
  // Set up digital button callbacks
  _userButton->onShortPress([this]() { handleButtonShortPress(); });
  _userButton->onDoublePress([this]() { handleButtonDoublePress(); });
  _userButton->onTriplePress([this]() { handleButtonTriplePress(); });
  _userButton->onQuadruplePress([this]() { handleButtonQuadruplePress(); });
  _userButton->onLongPress([this]() { handleButtonLongPress(); });
  _userButton->onAnyPress([this]() { handleButtonAnyPress(); });
#endif

  // Initialize analog button if available
#ifdef PIN_USER_BTN_ANA
  _userButtonAnalog = new Button(PIN_USER_BTN_ANA, USER_BTN_PRESSED, true, 20);
  _userButtonAnalog->begin();
  
  // Set up analog button callbacks
  _userButtonAnalog->onShortPress([this]() { handleButtonShortPress(); });
  _userButtonAnalog->onDoublePress([this]() { handleButtonDoublePress(); });
  _userButtonAnalog->onTriplePress([this]() { handleButtonTriplePress(); });
  _userButtonAnalog->onQuadruplePress([this]() { handleButtonQuadruplePress(); });
  _userButtonAnalog->onLongPress([this]() { handleButtonLongPress(); });
  _userButtonAnalog->onAnyPress([this]() { handleButtonAnyPress(); });
#endif
  ui_started_at = millis();
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif
//  Serial.print("DBG:  Alert user -> ");
//  Serial.println((int) t);
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    clearMsgPreview();
  }
}

void UITask::clearMsgPreview() {
  _origin[0] = 0;
  _msg[0] = 0;
  _need_refresh = true;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

  if (path_len == 0xFF) {
    sprintf(_origin, "(F) %s", from_name);
  } else {
    sprintf(_origin, "(%d) %s", (uint32_t) path_len, from_name);
  }
  StrHelper::strncpy(_msg, text, sizeof(_msg));

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
    _need_refresh = true;
    }
  }
}

void UITask::renderBatteryIndicator(uint16_t batteryMilliVolts) {
  // Convert millivolts to percentage
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
  const int minMilliVolts = BATT_MIN_MILLIVOLTS;
  const int maxMilliVolts = BATT_MAX_MILLIVOLTS;
  int batteryPercentage = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
  if (batteryPercentage < 0) batteryPercentage = 0; // Clamp to 0%
  if (batteryPercentage > 100) batteryPercentage = 100; // Clamp to 100%

  // battery icon
  int iconWidth = 24;
  int iconHeight = 12;
  int iconX = _display->width() - iconWidth - 5; // Position the icon near the top-right corner
  int iconY = 0;
  _display->setColor(DisplayDriver::GREEN);

  // battery outline
  _display->drawRect(iconX, iconY, iconWidth, iconHeight);

  // battery "cap"
  _display->fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

  // fill the battery based on the percentage
  int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
  _display->fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);
}

void UITask::renderCurrScreen() {
  if (_display == NULL) return;  // assert() ??

  char tmp[80];
  if (_alert[0]) {
    _display->setTextSize(1.4);
    uint16_t textWidth = _display->getTextWidth(_alert);
    _display->setCursor((_display->width() - textWidth) / 2, 22);
    _display->setColor(DisplayDriver::GREEN);
    _display->print(_alert);
    _alert[0] = 0;
    _need_refresh = true;
    return;
  } else if (_origin[0] && _msg[0]) { // message preview
    // render message preview
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::GREEN);
    _display->print(_node_prefs->node_name);

    _display->setCursor(0, 12);
    _display->setColor(DisplayDriver::YELLOW);
    _display->print(_origin);
    _display->setCursor(0, 24);
    _display->setColor(DisplayDriver::LIGHT);
    _display->print(_msg);

    _display->setCursor(_display->width() - 28, 9);
    _display->setTextSize(2);
    _display->setColor(DisplayDriver::ORANGE);
    sprintf(tmp, "%d", _msgcount);
    _display->print(tmp);
    _display->setColor(DisplayDriver::YELLOW); // last color will be kept on T114
  } else if ((millis() - ui_started_at) < BOOT_SCREEN_MILLIS) { // boot screen
    // meshcore logo
    _display->setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    _display->setColor(DisplayDriver::LIGHT);
    _display->setTextSize(1);
    uint16_t textWidth = _display->getTextWidth(_version_info);
    _display->setCursor((_display->width() - textWidth) / 2, 22);
    _display->print(_version_info);
  } else {  // home screen
    // node name
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::GREEN);
    _display->print(_node_prefs->node_name);

    // battery voltage
    renderBatteryIndicator(_board->getBattMilliVolts());

    // freq / sf
    _display->setCursor(0, 20);
    _display->setColor(DisplayDriver::YELLOW);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    // bw / cr
    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);

    // BT pin
    if (!_connected && the_mesh.getBLEPin() != 0) {
      _display->setColor(DisplayDriver::RED);
      _display->setTextSize(2);
      _display->setCursor(0, 43);
      sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
      _display->print(tmp);
      _display->setColor(DisplayDriver::GREEN);
    } else {
      _display->setColor(DisplayDriver::LIGHT); 
    }
  }
  _need_refresh = false;
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  static int state = 0;
  static int next_change = 0;
  static int last_increment = 0;

  int cur_time = millis();
  if (cur_time > next_change) {
    if (state == 0) {
      state = 1;
      if (_msgcount > 0) {
        last_increment = LED_ON_MSG_MILLIS;
      } else {
        last_increment = LED_ON_MILLIS;
      }
      next_change = cur_time + last_increment;
    } else {
      state = 0;
      next_change = cur_time + LED_CYCLE_MILLIS - last_increment;
    }
    digitalWrite(PIN_STATUS_LED, state == LED_STATE_ON);
  }
#endif
}

/* 
  hardware-agnostic pre-shutdown activity should be done here 
*/
void UITask::shutdown(bool restart){

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    radio_driver.powerOff();
    _board->powerOff();
  }
}

void UITask::loop() {
  #ifdef PIN_USER_BTN
    if (_userButton) {
      _userButton->update();
    }
  #endif
  #ifdef PIN_USER_BTN_ANA
    if (_userButtonAnalog) {
      _userButtonAnalog->update();
    }
  #endif
  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (_display != NULL && _display->isOn()) {
    static bool _firstBoot = true;
    if(_firstBoot && (millis() - ui_started_at) >= BOOT_SCREEN_MILLIS) {
      _need_refresh = true;
      _firstBoot = false;
    }
    if (millis() >= _next_refresh && _need_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 1000;   // refresh every second
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}

void UITask::handleButtonAnyPress() {
  MESH_DEBUG_PRINTLN("UITask: any press triggered");
  // called on any button press before other events, to wake up the display quickly
  // do not refresh the display here, as it may block the button handler
  if (_display != NULL) {
    _displayWasOn = _display->isOn();  // Track display state before any action
    if (!_displayWasOn) {
      _display->turnOn();
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
  }
}

void UITask::handleButtonShortPress() {
  MESH_DEBUG_PRINTLN("UITask: short press triggered");
  if (_display != NULL) {
    // Only clear message preview if display was already on before button press
    if (_displayWasOn) {
      // If display was on and showing message preview, clear it
      if (_origin[0] && _msg[0]) {
        clearMsgPreview();
      } else {
        // Otherwise, refresh the display
        _need_refresh = true;
      }
    } else {
      _need_refresh = true; // display just turned on, so we need to refresh
    }
    // Note: Display turn-on and auto-off timer extension are handled by handleButtonAnyPress
  }
}

void UITask::handleButtonDoublePress() {
  MESH_DEBUG_PRINTLN("UITask: double press triggered, sending advert");
  // ADVERT
  #ifdef PIN_BUZZER
      notify(UIEventType::ack);
  #endif
  if (the_mesh.advert()) {
    MESH_DEBUG_PRINTLN("Advert sent!");
    sprintf(_alert, "Advert sent!");
  } else {
    MESH_DEBUG_PRINTLN("Advert failed!");
    sprintf(_alert, "Advert failed..");
  }
  _need_refresh = true;
}

void UITask::handleButtonTriplePress() {
  MESH_DEBUG_PRINTLN("UITask: triple press triggered");
  // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
      sprintf(_alert, "Buzzer: ON");
    } else {
      buzzer.quiet(true);
      sprintf(_alert, "Buzzer: OFF");
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    _need_refresh = true;
  #endif
}

void UITask::handleButtonQuadruplePress() {
  MESH_DEBUG_PRINTLN("UITask: quad press triggered");
  if (_sensors != NULL) {
    // toggle GPS onn/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          notify(UIEventType::ack);
          sprintf(_alert, "GPS: Disabled");
        } else {
          _sensors->setSettingValue("gps", "1");
          notify(UIEventType::ack);
          sprintf(_alert, "GPS: Enabled");
        }
        break;
      }
    }
  }
  _need_refresh = true;
}

void UITask::handleButtonLongPress() {
  MESH_DEBUG_PRINTLN("UITask: long press triggered");
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
  } else {
    shutdown();
  }
}