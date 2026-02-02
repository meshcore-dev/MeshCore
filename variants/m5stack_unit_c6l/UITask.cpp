#include "UITask.h"
#include <target.h>
#include "../examples/companion_radio/MyMesh.h"

#define AUTO_OFF_MILLIS  30000  // 30 seconds

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _node_prefs = node_prefs;
  _need_refresh = true;
  _msgcount = 0;
  _next_refresh = 0;
  _auto_off = millis() + AUTO_OFF_MILLIS;

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

  char tmp[32];
  uint32_t pin = the_mesh.getBLEPin();

  // 64x48 display, 6x8 default font = 10 chars x 6 lines
  // No offsets - let Adafruit library handle it

  // Line 1: PIN or status (y=0)
  _display->setTextSize(1);
  _display->setCursor(0, 0);
  if (_connected) {
    sprintf(tmp, "Connected");
  } else if (pin != 0) {
    sprintf(tmp, "PIN:%06d", pin);
  } else {
    sprintf(tmp, "Ready");
  }
  _display->print(tmp);

  // Line 2: Node name (y=8)
  _display->setCursor(0, 8);
  char name[11];
  strncpy(name, _node_prefs->node_name, 10);
  name[10] = '\0';
  _display->print(name);

  // Line 3: Frequency (y=16)
  _display->setCursor(0, 16);
  sprintf(tmp, "%.2fMHz", _node_prefs->freq);
  _display->print(tmp);
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
    if (millis() >= _next_refresh && _need_refresh) {
      _display->startFrame();
      renderScreen();
      _display->endFrame();
      _next_refresh = millis() + 1000;
      _need_refresh = false;
    }

    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
