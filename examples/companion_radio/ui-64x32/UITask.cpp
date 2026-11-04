#include "UITask.h"

#include "../MyMesh.h"
#include "target.h"

#define UI_REFRESH_MILLIS          250
#define UI_FOOTER_ALTERNATE_MILLIS 2000

void UITask::begin(DisplayDriver *display, SensorManager *sensors, NodePrefs *node_prefs) {
  (void)sensors;
  (void)node_prefs;

  _display = display;
  _started_at = millis();

#ifdef PIN_USER_BTN
  user_btn.begin();
#endif

  wakeDisplay();
}

void UITask::wakeDisplay() {
  if (_display == NULL) return;

  if (!_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _footer_started_at = millis();
  _next_refresh = 0;
}

void UITask::render() {
  char status[16];
  char messages[16];
  char node_id[10];
  char node_name[32];

  if (hasConnection()) {
    strcpy(status, "CONNECTED");
  } else if (!isBluetoothEnabled()) {
    strcpy(status, "BT OFF");
  } else if (the_mesh.getBLEPin() != 0) {
    snprintf(status, sizeof(status), "PIN %06lu", (unsigned long)the_mesh.getBLEPin());
  } else {
    strcpy(status, "READY");
  }

  snprintf(messages, sizeof(messages), "MSG: %d", _msgcount);

  strcpy(node_id, "ID ");
  mesh::Utils::toHex(&node_id[3], the_mesh.self_id.pub_key, 3);
  _display->translateUTF8ToBlocks(node_name, the_mesh.getNodeName(), sizeof(node_name));

  _display->startFrame();
  _display->setColor(UIColor::primary_txt);
  _display->setTextSize(1);
  _display->drawTextCentered(_display->width() / 2, 0, status);
  _display->drawTextCentered(_display->width() / 2, 12, messages);
  bool show_id = ((millis() - _footer_started_at) / UI_FOOTER_ALTERNATE_MILLIS) % 2;
  if (show_id) {
    _display->drawTextCentered(_display->width() / 2, 24, node_id);
  } else if (_display->getTextWidth(node_name) <= _display->width()) {
    _display->drawTextCentered(_display->width() / 2, 24, node_name);
  } else {
    _display->drawTextEllipsized(0, 24, _display->width(), node_name);
  }
  _display->endFrame();
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  _next_refresh = 0;
}

void UITask::newMsg(uint8_t path_len, const char *from_name, const char *text, int msgcount) {
  (void)path_len;
  (void)from_name;
  (void)text;

  _msgcount = msgcount;
  wakeDisplay();
}

void UITask::notify(UIEventType type) {
  (void)type;
}

void UITask::loop() {
#ifdef PIN_USER_BTN
  int event = user_btn.check();
  if (event == BUTTON_EVENT_CLICK || event == BUTTON_EVENT_DOUBLE_CLICK ||
      event == BUTTON_EVENT_TRIPLE_CLICK) {
    if (_display != NULL && _display->isOn()) {
      _display->turnOff();
    } else {
      wakeDisplay();
    }
  } else if (event == BUTTON_EVENT_LONG_PRESS && millis() - _started_at < 8000) {
    the_mesh.enterCLIRescue();
  }
#endif

  if (_display == NULL || !_display->isOn()) return;

  if (millis() >= _next_refresh) {
    render();
    _next_refresh = millis() + UI_REFRESH_MILLIS;
  }

#if AUTO_OFF_MILLIS > 0
  if (millis() >= _auto_off) {
    _display->turnOff();
  }
#endif
}
