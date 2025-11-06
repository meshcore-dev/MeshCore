#include "QuickMsg.h"

#if UI_QUICK_MSG
#include "../MyMesh.h"

#ifndef COUNTOF
  #define COUNTOF(x) sizeof(x) / sizeof(x[0])
#endif

static constexpr const char* messages[] {
  "test", "ping", "hello", "ack", "yes", "no", "share location",
  "come to me", "going to you", "help", "SOS"
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

    // Next value/Use button
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

    // Prev value
    case KEY_SELECT:
      if (_row == Row::MSG)
        nextMessage(/*fwd=*/false);
      else if (_row == Row::CHANNEL)
        nextChannel(/*fwd=*/false);
      else
        MESH_DEBUG_PRINTLN("Bad row index");
      return true;
    
    default:
      _task->showAlert(PRESS_LABEL " to exit", 1000);
      break;
  }

  return false;
}

void QuickMsgScreen::nextMessage(bool fwd) {
  auto msg_count = messages_count;

#if ENV_INCLUDE_GPS
  // Make fake index for GPS if enabled.
  if (_task->getGPSState())
    msg_count += 1;
#endif

  _kind = MsgKind::TEXT;
  _msg_ix = (_msg_ix + (fwd ? 1 : msg_count - 1)) % msg_count;

#if ENV_INCLUDE_GPS
  // Index at end of messages, add fake GPS entry.
  if (_msg_ix == messages_count)
    _kind = MsgKind::GPS;
#endif
}

void QuickMsgScreen::nextChannel(bool fwd) {
#ifndef MAX_GROUP_CHANNELS
  _channel_ix = 0;
  return;
#else
  ChannelDetails details;
  _channel_ix = (_channel_ix + (fwd ? 1 : MAX_GROUP_CHANNELS - 1)) % MAX_GROUP_CHANNELS;
  the_mesh.getChannel(_channel_ix, details);

  if (fwd) {
    // Moving forward. Reset to 0 on first invalid channel.
    if (details.name[0] == 0)
      _channel_ix = 0;
  } else {
    // Moving backward. Find a valid channel or 0.
    while (_channel_ix > 0 && details.name[0] == 0)
      the_mesh.getChannel(--_channel_ix, details);
  }

  // Copy valid names to the UI buffer.
  if (details.name[0] != 0)
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
#if ENV_INCLUDE_GPS
  if (_kind == MsgKind::GPS) {
    LocationProvider* nmea = sensors.getLocationProvider();
    if (!nmea) {
      sprintf(_msg_text, "GPS Error");
    } else {
      if (nmea->isValid()) {
        sprintf(_msg_text, "%.4f %.4f", 
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
      } else {
        sprintf(_msg_text, "No GPS fix");
      }
    }
    return _msg_text;
  }
#endif

  return _msg_ix < messages_count ? messages[_msg_ix] : "???";
}

const char* QuickMsgScreen::getChannelName() {
  return _channel_ix == 0 ? "public" : _channel_name;
}

#endif