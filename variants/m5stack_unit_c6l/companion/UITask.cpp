#include "UITask.h"
#include <target.h>
#include "../../examples/companion_radio/MyMesh.h"

#define AUTO_OFF_MILLIS    30000  // 30 seconds
#define BOOT_SCREEN_MILLIS 4000  // 4 seconds
#define SCROLL_SPEED_MS    150   // pixels per tick
#define SCROLL_PAUSE_MS    2000  // pause at start/end

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _node_prefs = node_prefs;
  _need_refresh = true;
  _msgcount = 0;
  _next_refresh = 0;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _scroller.reset();

  Serial.println("UITask: begin()");
  if (_display != NULL) {
    Serial.println("UITask: calling turnOn()");
    _display->turnOn();
    Serial.print("UITask: isOn() = ");
    Serial.println(_display->isOn());
  } else {
    Serial.println("UITask: display is NULL");
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  _need_refresh = true;
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }
}

void UITask::renderScreen() {
  if (_display == NULL) return;

  int w = _display->width();
  char tmp[32];

  if (millis() < BOOT_SCREEN_MILLIS) {
    // Boot screen
    _display->setTextSize(1);
    _display->drawTextCentered(w / 2, 3, "MeshCore");
    _display->drawTextCentered(w / 2, 20, FIRMWARE_VERSION);
    _display->drawTextCentered(w / 2, 34, "Companion");
    return;
  }

  _display->setTextSize(1);

  // Line 1: status (y=0)
#ifdef BLE_PIN_CODE
  uint32_t pin = the_mesh.getBLEPin();
  if (_connected) {
    _display->drawTextCentered(w / 2, 0, "Connected");
  } else if (pin != 0) {
    sprintf(tmp, "PIN:%06d", pin);
    _display->drawTextCentered(w / 2, 0, tmp);
  } else {
    _display->drawTextCentered(w / 2, 0, "Ready");
  }
#else
  _display->drawTextCentered(w / 2, 0, "USB Ready");
#endif

  // Line 2: Node name with marquee scroll (y=10)
  int nameW = _display->getTextWidth(_node_prefs->node_name);
  if (nameW <= w) {
    // Fits on screen, no scroll needed
    _display->setCursor(0, 10);
    _display->print(_node_prefs->node_name);
  } else {
    // Marquee: draw at negative offset
    _display->setCursor(-_scroller.offset, 10);
    _display->print(_node_prefs->node_name);
  }

  // Line 3: Frequency (y=20)
  sprintf(tmp, "%.3f", _node_prefs->freq);
  _display->setCursor(0, 20);
  _display->print(tmp);

  // Line 4: Unread messages (y=30)
  if (_msgcount > 0) {
    sprintf(tmp, "%d unread", _msgcount);
    _display->setCursor(0, 30);
    _display->print(tmp);
  }
}

void UITask::loop() {
  if (_display == NULL) return;

  // Check button press to wake display
  if (board.isButtonPressed()) {
    if (!_display->isOn()) {
      _display->turnOn();
      _need_refresh = true;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }

  if (_display->isOn()) {
    // Update marquee scroll
    if (_node_prefs != NULL) {
      _scroller.update(_display->getTextWidth(_node_prefs->node_name),
                       _display->width(), millis(), SCROLL_SPEED_MS, SCROLL_PAUSE_MS);
    }

    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderScreen();
      _display->endFrame();
      _next_refresh = millis() + 200;  // faster refresh for smooth scroll
    }

    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
