#pragma once

#include "UITask.h"

#if UI_QUICK_MSG
#include <helpers/ChannelDetails.h>
#include <helpers/ui/UIScreen.h>

class QuickMsgScreen : public UIScreen {
  enum Row {
    MSG,
    CHANNEL,
    SEND,
    Count
  };

  enum MsgKind {
    TEXT,
#if ENV_INCLUDE_GPS
    GPS,
#endif
  };

  UITask* _task;
  uint8_t _msg_ix = 0;
  uint8_t _channel_ix = 0;
  uint8_t _kind = MsgKind::TEXT;
  uint8_t _row = 0;
  uint8_t _row_defs[Row::Count] { 20, 35, 50 };
  char _channel_name[32];  // see ChannelDetails.h
#if ENV_INCLUDE_GPS
  char _msg_text[32];  // Buffer for dynamic messages.
#endif

  void nextMessage();
  void nextChannel();
  void sendMessage();

  const char* getMessageText();
  const char* getChannelName();

public:
  QuickMsgScreen(UITask* task);
  int render(DisplayDriver& display) override;
  bool handleInput(char c) override;
};

#endif