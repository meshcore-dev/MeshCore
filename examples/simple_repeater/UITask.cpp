#include "UITask.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#ifndef USER_BTN_PRESSED
#define USER_BTN_PRESSED LOW
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds
#define STATUS_SCREEN_MILLIS 5000   // 5 seconds

// Electric plug icon 16x7px (side profile, cord left, prongs right)
static const uint8_t charging_icon [] PROGMEM = {
  0x01, 0xE0,  // .......####.....
  0x03, 0xE0,  // ......#####.....
  0x03, 0xFE,  // ......#########.
  0xFF, 0xE0,  // ###########.....
  0x03, 0xFE,  // ......#########.
  0x03, 0xE0,  // ......#####.....
  0x01, 0xE0,  // .......####.....
};

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

// Draw a horizontal rule
static void drawHRule(DisplayDriver* d, int x, int y, int w) {
  d->setColor(DisplayDriver::LIGHT);
  d->fillRect(x, y, w, 1);
}

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _display->turnOn();

  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if(dash) *dash = 0;

  sprintf(_version_info, "%s (%s)", version, build_date);
}

void UITask::showStatus(uint16_t batt_mv, unsigned long uptime_ms, bool charging) {
  _status_batt_mv = batt_mv;
  _status_uptime_ms = uptime_ms;
  _status_charging = charging;
  _status_until = millis() + STATUS_SCREEN_MILLIS;
  _auto_off = _status_until + AUTO_OFF_MILLIS;
  _next_refresh = 0;
  _display->turnOn();
}

void UITask::renderCurrScreen() {
  char tmp[80];
  int W = _display->width();

  if (millis() < BOOT_SCREEN_MILLIS) {
    // ── BOOT SCREEN ──

    // logo centered
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawXbm((W - 128) / 2, 6, meshcore_logo, 128, 13);

    // thin rule under logo
    drawHRule(_display, 10, 22, W - 20);

    // version centered
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::LIGHT);
    _display->drawTextCentered(W / 2, 27, _version_info);

    // role label in brackets
    drawHRule(_display, 10, 39, W - 20);
    _display->drawTextCentered(W / 2, 44, "[ REPEATER ]");

  } else if (_status_until > 0 && millis() < _status_until) {
    // ── STATUS SCREEN ──

    // header bar: inverted "STATUS" label
    _display->setColor(DisplayDriver::LIGHT);
    _display->fillRect(0, 0, W, 11);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::DARK);
    _display->drawTextCentered(W / 2, 2, "STATUS");

    // Battery: clamp to 0-100%
    int pct = 0;
    if (_status_batt_mv >= 4200) pct = 100;
    else if (_status_batt_mv > 3000) pct = (_status_batt_mv - 3000) * 100 / 1200;

    // uptime row
    _display->setColor(DisplayDriver::LIGHT);
    unsigned long secs = _status_uptime_ms / 1000;
    unsigned long mins = secs / 60;
    unsigned long hrs  = mins / 60;
    unsigned long days = hrs / 24;

    _display->setCursor(4, 15);
    _display->print("UPTIME");
    if (days > 0) {
      sprintf(tmp, "%lud %luh %lum", days, hrs % 24, mins % 60);
    } else if (hrs > 0) {
      sprintf(tmp, "%luh %lum %lus", hrs, mins % 60, secs % 60);
    } else {
      sprintf(tmp, "%lum %lus", mins, secs % 60);
    }
    _display->drawTextRightAlign(W - 4, 15, tmp);

    drawHRule(_display, 4, 25, W - 8);

    // battery voltage row
    _display->setCursor(4, 29);
    _display->print("BATT");
    sprintf(tmp, "%u.%02uV  %d%%", _status_batt_mv / 1000, (_status_batt_mv % 1000) / 10, pct);
    _display->drawTextRightAlign(W - 4, 29, tmp);

    drawHRule(_display, 4, 39, W - 8);

    // battery bar (full width gauge)
    int barX = 4, barY = 43, barW = W - 8, barH = 8;
    _display->drawRect(barX, barY, barW, barH);
    int fillW = (pct * (barW - 4)) / 100;
    if (fillW > 0) _display->fillRect(barX + 2, barY + 2, fillW, barH - 4);

    // percentage label centered under bar, with charging icon if applicable
    sprintf(tmp, "%d%%", pct);
    if (_status_charging) {
      int txtW = _display->getTextWidth(tmp);
      int totalW = 18 + 2 + txtW + 4 + _display->getTextWidth("CHG");
      int startX = (W - totalW) / 2;
      _display->drawXbm(startX, 55, charging_icon, 16, 7);
      _display->setCursor(startX + 20, 54);
      _display->print(tmp);
      _display->print(" CHG");
    } else {
      _display->drawTextCentered(W / 2, 54, tmp);
    }

  } else {
    // ── HOME SCREEN ──

    // header bar: inverted node name
    _display->setColor(DisplayDriver::LIGHT);
    _display->fillRect(0, 0, W, 11);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::DARK);
    _display->drawTextCentered(W / 2, 2, _node_prefs->node_name);

    // radio params
    _display->setColor(DisplayDriver::LIGHT);

    _display->setCursor(4, 16);
    _display->print("FREQ");
    sprintf(tmp, "%06.3f MHz", _node_prefs->freq);
    _display->drawTextRightAlign(W - 4, 16, tmp);

    drawHRule(_display, 4, 26, W - 8);

    _display->setCursor(4, 30);
    _display->print("SF");
    sprintf(tmp, "%d", _node_prefs->sf);
    _display->setCursor(4 + _display->getTextWidth("SF "), 30);
    _display->print(tmp);

    sprintf(tmp, "CR %d", _node_prefs->cr);
    _display->drawTextRightAlign(W - 4, 30, tmp);

    drawHRule(_display, 4, 40, W - 8);

    _display->setCursor(4, 44);
    _display->print("BW");
    sprintf(tmp, "%03.1f kHz", _node_prefs->bw);
    _display->drawTextRightAlign(W - 4, 44, tmp);
  }
}

void UITask::loop() {
#ifdef PIN_USER_BTN
  if (millis() >= _next_read) {
    int btnState = digitalRead(PIN_USER_BTN);
    if (btnState != _prevBtnState) {
      if (btnState == USER_BTN_PRESSED) {
        if (_display->isOn()) {
          // TODO: any action ?
        } else {
          _display->turnOn();
        }
        _auto_off = millis() + AUTO_OFF_MILLIS;
      }
      _prevBtnState = btnState;
    }
    _next_read = millis() + 200;
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 1000;
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
