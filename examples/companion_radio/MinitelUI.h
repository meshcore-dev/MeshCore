#pragma once

#include <Arduino.h>

// Minitel 1B terminal on a serial port: shows one group channel ('Public' by default)
// and lets the user type and send messages from the Minitel keyboard.
//
// Wiring (Minitel DIN socket):  DIN1 (Minitel RX) <- MINITEL_TX
//                               DIN3 (Minitel TX) -> MINITEL_RX, with a 10k pull-up to 3.3V (NOT 5V)
//                               DIN2 = GND

#ifndef MINITEL_UART
  #define MINITEL_UART  2
#endif
#ifndef MINITEL_CHANNEL_IDX
  #define MINITEL_CHANNEL_IDX  0     // 0 = 'Public'
#endif
#ifndef MINITEL_HISTORY
  #define MINITEL_HISTORY  24        // messages kept in RAM
#endif

#define MINITEL_MSG_SLOT    200      // bytes per stored message (sanitized "name: text")
#define MINITEL_INPUT_SIZE  200

class MyMesh;
class Minitel;
namespace mesh { class MainBoard; }

class MinitelUI {
public:
  MinitelUI(MyMesh& mesh, mesh::MainBoard& board) : _mesh(mesh), _board(board) { }

  void begin();
  void loop();

  // called for every channel message received, or sent from the companion app
  // sender == NULL: text is already formatted as "name: msg"
  void onChannelMessage(uint8_t channel_idx, const char* sender, const char* text, int text_len);

private:
  enum State { PROBE_WAIT, PROBE_QUERY, PROBE_SWITCH, ONLINE };
  enum RxState { RX_IDLE, RX_SEP, RX_SS2, RX_SS2_ACCENT, RX_ESC, RX_PRO2, RX_SKIP };

  struct LineRef {
    uint8_t msg;
    uint8_t off;
    uint8_t len;
  };

  MyMesh& _mesh;
  mesh::MainBoard& _board;
  Minitel* _mt = NULL;

  State _state = PROBE_WAIT;
  unsigned long _deadline = 0;
  unsigned long _next_ping = 0;
  bool _ping_pending = false;
  unsigned long _next_header = 0;

  RxState _rx = RX_IDLE;
  uint8_t _rx_accent = 0;
  uint8_t _rx_skip = 0;
  uint8_t _pro[2];
  uint8_t _pro_len = 0;

  char _msgs[MINITEL_HISTORY][MINITEL_MSG_SLOT];
  int _msg_head = 0;
  int _msg_count = 0;
  int _scroll = 0;   // lines scrolled back from the newest

  char _input[MINITEL_INPUT_SIZE];
  int _input_len = 0;
  int _input_chars = 0;

  bool _full_dirty = false;
  bool _msgs_dirty = false;
  bool _header_dirty = false;

  void writeRaw(const uint8_t* data, int len);
  void startProbe();
  void goOnline();
  void goOffline();
  void handleByte(uint8_t b);
  void onFunctionKey(uint8_t key);
  void onSpeedReply(uint8_t code);
  void typeText(const char* utf8);
  void backspace();
  void clearInput();
  void sendInput();
  int inputMaxBytes();

  int buildLines(LineRef* lines, int max_lines);
  void renderAll();
  void renderHeader();
  void renderMessages();
  void renderHint();
  void renderInput();
  void placeCursor();
};
