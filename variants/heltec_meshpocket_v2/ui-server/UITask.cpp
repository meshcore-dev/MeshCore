// Based on src/helpers/ui/server/UITask.cpp at upstream 88bbae29.
#include "UITask.h"
#include "target.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

#define POWEROFF_DELAY 3000

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

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version, const char* role_lbl) {
  powerUi.begin();
#ifdef MESHPOCKET_V2_ROOM_SERVER
  _prevBtnState = HIGH;
  _started_at = 0;  // The Room Server's splash starts at power-on.
#else
  _started_at = millis();
#endif
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _display->turnOn();

#ifndef MESHPOCKET_V2_ROOM_SERVER
  user_btn.begin();
#endif

  // strip off dash and commit hash by changing dash to null terminator
  // e.g: v1.2.3-abcdef -> v1.2.3
  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if (dash){
    *dash = 0;
  }

  // v1.2.3 (1 Jan 2025)
  snprintf(_version_info, sizeof(_version_info), "%s (%s)", version, build_date);
  free(version);

  _role_label = role_lbl;
}

long UITask::renderCurrScreen() {
  if (!_powering_off_at && _power_page_selected) {
    const long interval = powerUi.render(::display, _power_page_full_refresh);
    _power_page_full_refresh = false;
    return interval;
  }
  _display->startFrame();
  char tmp[80];
  if (millis() < _started_at + BOOT_SCREEN_MILLIS) { // boot screen
    // meshcore logo
    _display->setColor(UIColor::corp_blue);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // meshcore website
    const char* website = "https://meshcore.io";
    _display->setColor(UIColor::primary_txt);
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width() / 2, 22, website);

    // version info
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width() / 2, 35, _version_info);

    // node type
    _display->drawTextCentered(_display->width() / 2, 48, _role_label);
  } else if (_powering_off_at > 0) {
    // meshcore logo
    _display->setColor(UIColor::corp_blue);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // meshcore website
    const char* website = "https://meshcore.io";
    _display->setColor(UIColor::primary_txt);
    _display->setTextSize(1);
    _display->drawTextCentered(_display->width()/ 2, 22, website);

    // Powering off
    const char* poweroff_string = "Turning OFF";
    uint16_t poffWidth = _display->getTextWidth(poweroff_string);
    _display->setCursor((_display->width() - poffWidth) / 2, 48);
    _display->drawTextCentered(_display->width()/2, 48, poweroff_string);
  } else {
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(UIColor::primary_txt);
    _display->print(_node_prefs->node_name);

    // freq / sf
    _display->setCursor(0, 20);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    // bw / cr
    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);
  }
  return 1000;   // 1 sec interval
}

int UITask::readPrimaryButton() {
#ifdef MESHPOCKET_V2_ROOM_SERVER
  if (millis() < _next_read) return BUTTON_EVENT_NONE;
  const int state = digitalRead(PIN_USER_BTN);
  const bool pressed = state == USER_BTN_PRESSED && state != _prevBtnState;
  _prevBtnState = state;
  _next_read = millis() + 200;
  return pressed ? BUTTON_EVENT_CLICK : BUTTON_EVENT_NONE;
#else
  return user_btn.check();
#endif
}

void UITask::showPowerPage() {
  if (!_display->isOn()) _display->turnOn();
  if (!_power_page_selected) _power_page_full_refresh = true;
  _power_page_selected = true;
  _next_refresh = 0;
  _auto_off = millis() + AUTO_OFF_MILLIS;
}

void UITask::loop() {
  if (!_display->isOn()) _power_page_full_refresh = true;
  const uint8_t events = powerUi.pollEvents();
  const bool canChangePage = !_powering_off_at && millis() - _started_at >= BOOT_SCREEN_MILLIS;
  bool handlingInput = false;
  if (canChangePage && (events & PowerBankUi::EVENT_SHOW_PAGE)) {
    showPowerPage();
    handlingInput = true;
  } else if (canChangePage && (events & PowerBankUi::EVENT_PAGE_ACTION) &&
             _display->isOn() && _power_page_selected) {
    powerUi.handlePageAction();
    _next_refresh = 0;
    _auto_off = millis() + AUTO_OFF_MILLIS;
    handlingInput = true;
  }

  const int ev = readPrimaryButton();
  handlingInput = handlingInput || ev != BUTTON_EVENT_NONE;
  if (ev == BUTTON_EVENT_CLICK) {
    if (!_display->isOn()) {
      _display->turnOn();
    } else if (canChangePage) {
      _power_page_selected = !_power_page_selected;
      _power_page_full_refresh = true;
      _next_refresh = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    _display->turnOn();
    Serial.println("Powering Off");
    _powering_off_at = millis() + POWEROFF_DELAY;
    _next_refresh = 0;
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }

  const bool canTakePowerEvent = !_powering_off_at && millis() - _started_at >= BOOT_SCREEN_MILLIS;
  if (!handlingInput && canTakePowerEvent && (events & PowerBankUi::EVENT_REQUEST_FOCUS)) {
    showPowerPage();
  }

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      const long interval = renderCurrScreen();
      _display->endFrame();
      _next_refresh = millis() + interval;
    }
    if (millis() > _auto_off) _display->turnOff();
  }

  if (_powering_off_at > 0) { // power off timer armed
#if defined(LED_PIN) && defined(LED_STATE_ON)
    digitalWrite(LED_PIN, LED_STATE_ON); // switch on the led until poweroff
    delay(1000);
    digitalWrite(LED_PIN, !LED_STATE_ON); // off LED
#endif
    if (millis() > _powering_off_at) {
      _board->powerOff();  // should not return
    }
  }
}
