#include "UITask.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

static char alert_msg[64] = "";
static unsigned long alert_until = 0;

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

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

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
#ifdef HELTEC_V3_SCREEN_LED_CONTROL
  bool screen_enabled = true;
  if (_board && _board->supportsDisplaySettings()) {
    screen_enabled = _board->getDisplayEnabled();
  }
  if (screen_enabled) {
    _display->turnOn();
  } else {
    _display->turnOff();
  }
#else
  _display->turnOn();
#endif

  // strip off dash and commit hash by changing dash to null terminator
  // e.g: v1.2.3-abcdef -> v1.2.3
  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if(dash){
    *dash = 0;
  }

  // v1.2.3 (1 Jan 2025)
  sprintf(_version_info, "%s (%s)", version, build_date);
}

void UITask::renderCurrScreen() {
  char tmp[80];
  if (millis() < BOOT_SCREEN_MILLIS) { // boot screen
    // meshcore logo
    _display->setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    _display->setColor(DisplayDriver::LIGHT);
    _display->setTextSize(1);
    uint16_t versionWidth = _display->getTextWidth(_version_info);
    _display->setCursor((_display->width() - versionWidth) / 2, 22);
    _display->print(_version_info);

    // node type
    const char* node_type = "< Repeater >";
    uint16_t typeWidth = _display->getTextWidth(node_type);
    _display->setCursor((_display->width() - typeWidth) / 2, 35);
    _display->print(node_type);
  } else {  // home screen
    // node name
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::GREEN);
    _display->print(_node_prefs->node_name);

    // freq / sf
    _display->setCursor(0, 20);
    _display->setColor(DisplayDriver::YELLOW);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    // bw / cr
    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);
  }

  // Show alert if active
  if (millis() < alert_until && alert_msg[0] != '\0') {
    _display->setColor(DisplayDriver::YELLOW);
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width() / 2, 50, alert_msg);
  }
}

void UITask::showAlert(const char* msg, int duration_ms) {
  strncpy(alert_msg, msg, sizeof(alert_msg) - 1);
  alert_msg[sizeof(alert_msg) - 1] = '\0';
  alert_until = millis() + duration_ms;
  _next_refresh = 0;  // trigger immediate refresh
}

void UITask::loop() {
#ifdef PIN_USER_BTN
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    if (!_display->isOn()) {
#ifdef HELTEC_V3_SCREEN_LED_CONTROL
      // Check if screen is enabled before turning on
      bool screen_enabled = true;
      if (_board && _board->supportsDisplaySettings()) {
        screen_enabled = _board->getDisplayEnabled();
      }
      if (screen_enabled) {
        _display->turnOn();
      }
#else
      _display->turnOn();
#endif
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;
#ifdef HELTEC_V3_SCREEN_LED_CONTROL
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    // Toggle screen
    if (_board && _board->supportsDisplaySettings()) {
      bool enabled = _board->getDisplayEnabled();
      _board->setDisplayEnabled(!enabled);
      showAlert(enabled ? "Screen: OFF" : "Screen: ON", 1500);
      if (!enabled) {
        _display->turnOn();
      }
      _next_refresh = 0;
    }
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    // Toggle LED
    if (_sensors != NULL) {
      int num = _sensors->getNumSettings();
      for (int i = 0; i < num; i++) {
        if (strcmp(_sensors->getSettingName(i), "led") == 0) {
          bool enabled = (strcmp(_sensors->getSettingValue(i), "1") == 0);
          _sensors->setSettingValue("led", enabled ? "0" : "1");
          showAlert(enabled ? "LED: OFF" : "LED: ON", 800);
          _next_refresh = 0;
          break;
        }
      }
    }
#endif
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

#ifdef HELTEC_V3_SCREEN_LED_CONTROL
      if (_board && _board->supportsDisplaySettings() && !_board->getDisplayEnabled()) {
        _display->turnOff();
      }
#endif

      _next_refresh = millis() + 1000;   // refresh every second
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
