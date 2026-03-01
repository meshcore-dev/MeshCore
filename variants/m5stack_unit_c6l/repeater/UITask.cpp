// C6L-specific repeater UITask for 64x48 SSD1306 display
// Replaces examples/simple_repeater/UITask.cpp via build_src_filter

#include "../../../examples/simple_repeater/UITask.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>
#include <M5StackUnitC6LBoard.h>

extern M5StackUnitC6LBoard board;

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds
#define SCROLL_SPEED_MS      150
#define SCROLL_PAUSE_MS      2000

static int s_scroll_offset = 0;
static unsigned long s_scroll_next = 0;
static bool s_scroll_paused = true;

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  s_scroll_offset = 0;
  s_scroll_next = 0;
  s_scroll_paused = true;
  _display->turnOn();

  char version[16];
  strncpy(version, firmware_version, sizeof(version) - 1);
  version[sizeof(version) - 1] = '\0';
  char *dash = strchr(version, '-');
  if (dash) *dash = '\0';
  sprintf(_version_info, "%s (%s)", version, build_date);
}

void UITask::renderCurrScreen() {
  char tmp[80];
  int w = _display->width();

  if (millis() < BOOT_SCREEN_MILLIS) {
    _display->setColor(DisplayDriver::LIGHT);
    _display->setTextSize(1);
    _display->drawTextCentered(w / 2, 3, "MeshCore");

    char short_ver[16];
    strncpy(short_ver, _version_info, sizeof(short_ver) - 1);
    short_ver[sizeof(short_ver) - 1] = '\0';
    char* paren = strchr(short_ver, ' ');
    if (paren) *paren = '\0';
    _display->drawTextCentered(w / 2, 20, short_ver);
    _display->drawTextCentered(w / 2, 34, "Repeater");
  } else {
    _display->setTextSize(1);

    // Line 1: Node name with marquee (y=0)
    _display->setColor(DisplayDriver::GREEN);
    int nameW = _display->getTextWidth(_node_prefs->node_name);
    if (nameW <= w) {
      _display->setCursor(0, 0);
      _display->print(_node_prefs->node_name);
    } else {
      _display->setCursor(-s_scroll_offset, 0);
      _display->print(_node_prefs->node_name);
    }

    // Line 2: Frequency (y=12)
    _display->setColor(DisplayDriver::YELLOW);
    sprintf(tmp, "%.3f", _node_prefs->freq);
    _display->setCursor(0, 12);
    _display->print(tmp);

    // Line 3: SF CR (y=24)
    sprintf(tmp, "SF%d CR%d", _node_prefs->sf, _node_prefs->cr);
    _display->setCursor(0, 24);
    _display->print(tmp);

    // Line 4: BW (y=36)
    sprintf(tmp, "BW%.1f", _node_prefs->bw);
    _display->setCursor(0, 36);
    _display->print(tmp);
  }
}

void UITask::loop() {
  // C6L button is on I2C expander, not a GPIO pin
  if (millis() >= _next_read) {
    if (board.isButtonPressed()) {
      if (!_display->isOn()) {
        _display->turnOn();
      }
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }
    _next_read = millis() + 200;
  }

  if (_display->isOn()) {
    // Update marquee scroll
    if (_node_prefs != NULL && millis() >= s_scroll_next) {
      int nameW = _display->getTextWidth(_node_prefs->node_name);
      int w = _display->width();
      int maxScroll = nameW - w;
      if (maxScroll > 0) {
        if (s_scroll_paused) {
          s_scroll_paused = false;
          s_scroll_next = millis() + SCROLL_PAUSE_MS;
        } else {
          s_scroll_offset++;
          if (s_scroll_offset >= maxScroll) {
            s_scroll_offset = 0;
            s_scroll_paused = true;
          }
          s_scroll_next = millis() + SCROLL_SPEED_MS;
        }
      }
    }

    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();
      _next_refresh = millis() + 200;
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
