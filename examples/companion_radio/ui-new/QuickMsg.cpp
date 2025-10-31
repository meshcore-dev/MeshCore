#include "QuickMsg.h"

#if UI_QUICK_MSG
#include "../MyMesh.h"

#ifndef COUNTOF
  #define COUNTOF(x) sizeof(x) / sizeof(x[0])
#endif

static constexpr const char* messages[] {
  "ping", "ack", "yes", "no", "test"
};
static constexpr size_t messages_count = COUNTOF(messages);

QuickMsgScreen::QuickMsgScreen(UITask* task)
  : _task(task) {
}

int QuickMsgScreen::render(DisplayDriver& display) {
  display.setColor(DisplayDriver::YELLOW);
  display.setTextSize(2);
  display.drawTextCentered(display.width() / 2, 2, "quick messages");

  display.setColor(DisplayDriver::GREEN);
  display.setTextSize(1);
  display.setCursor(2, _row_defs[0]);
  display.print("message:");
  display.setCursor(42, _row_defs[0]);
  display.print(getMessageText());

  display.setCursor(2, _row_defs[1]);
  display.print("channel:");
  display.setCursor(42, _row_defs[1]);
  display.print(getChannelName());

  display.drawTextCentered(display.width() / 2, _row_defs[2], "[send]");

  auto cursor_row = _row_defs[_row];
  display.drawRect(0, cursor_row - 1, display.width(), 12);
  return 1000;
}

bool QuickMsgScreen::handleInput(char c) {
  switch (c) {
    case KEY_ENTER:
      _task->gotoHomeScreen();
      return true;

    // Move cursor
    case KEY_PREV:
    case KEY_LEFT:
      _row = (_row + 1) % Row::Count;
      return true;

    // Update value/Use button
    case KEY_NEXT:
    case KEY_RIGHT:
      if (_row == Row::MSG)
        nextMessage();
      else if (_row == Row::CHANNEL)
        nextChannel();
      else if (_row == Row::SEND)
        sendMessage();
      else
        MESH_DEBUG_PRINTLN("Bad row index");
      return true;
    
    default:
      _task->showAlert(PRESS_LABEL " to exit", 1000);
      break;
  }

  return false;
}

void QuickMsgScreen::nextMessage() {
  _msg_ix = (_msg_ix + 1) % messages_count;
}

void QuickMsgScreen::nextChannel() {
#ifndef MAX_GROUP_CHANNELS
  _channel_ix = 0;
  return;
#else
  ChannelDetails details;
  _channel_ix = _channel_ix + 1 % MAX_GROUP_CHANNELS;

  the_mesh.getChannel(_channel_ix, details);

  // Channel slots are static, only cycle through valid entries.
  if (strlen(details.name) == 0)
    _channel_ix = 0;
  else
    strcpy(_channel_name, details.name);
#endif
}

void QuickMsgScreen::sendMessage() {
  ChannelDetails details;
  bool sent = false;

  if (the_mesh.getChannel(_channel_ix, details)) {
    auto now = the_mesh.getRTCClock()->getCurrentTime();
    auto name = the_mesh.getNodeName();
    auto text = getMessageText();
    auto len = strlen(text);

    if (the_mesh.sendGroupMessage(now, details.channel, name, text, len))
      sent = true;
  }

  if (sent)
    _task->showAlert("Message sent!", 1000);
  else
    _task->showAlert("Message failed.", 1000);
}

const char* QuickMsgScreen::getMessageText() {
  // TODO: special messages like GPS position and node name.
  return messages[_msg_ix];
}

const char* QuickMsgScreen::getChannelName() {
  return _channel_ix == 0 ? "public" : _channel_name;
}

#endif