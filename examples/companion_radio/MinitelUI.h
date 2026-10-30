#pragma once

#include <Arduino.h>

// Minitel 1B terminal on a serial port: shows group channels and private conversations,
// and lets the user type and send messages from the Minitel keyboard.
//
// Wiring (Minitel DIN socket):  DIN1 (Minitel RX) <- MINITEL_TX
//                               DIN3 (Minitel TX) -> MINITEL_RX, with a 10k pull-up to 3.3V (NOT 5V)
//                               DIN2 = GND

#ifndef MINITEL_UART
  #define MINITEL_UART  2
#endif
#ifndef MINITEL_CHANNEL_IDX
  #define MINITEL_CHANNEL_IDX  0     // conversation shown at start-up (0 = 'Public')
#endif
#ifndef MINITEL_HISTORY
  #define MINITEL_HISTORY  40        // messages kept in RAM, all conversations together
#endif

#define MINITEL_MSG_SLOT    200      // bytes per stored message (sanitized "name: text")
#define MINITEL_INPUT_SIZE  200

#define MINITEL_HOPS_DIRECT  0xFF    // received through a direct route (not flooded)
#define MINITEL_HOPS_LOCAL   0xFE    // sent from this node

class MyMesh;
class Minitel;
struct ContactInfo;
namespace mesh { class MainBoard; }

class MinitelUI {
public:
  MinitelUI(MyMesh& mesh, mesh::MainBoard& board) : _mesh(mesh), _board(board) { }

  void begin();
  void loop();

  // hooks called by MyMesh
  // sender == NULL: text is already formatted as "name: msg"
  void onChannelMessage(uint8_t channel_idx, const char* sender, const char* text, int text_len,
                        uint8_t hops = MINITEL_HOPS_LOCAL, int8_t snr4 = 0);
  void onDirectMessage(const ContactInfo& from, const char* text, uint8_t hops, int8_t snr4);
  void onDirectSent(const ContactInfo& to, const char* text, uint32_t expected_ack, uint32_t est_timeout);
  void onAck(uint32_t ack);

  // called from the UART event task
  void onRxError() { _rx_error = true; }

private:
  enum State { PROBE_WAIT, PROBE_QUERY, PROBE_SWITCH, ONLINE };
  enum Mode { MODE_SPLASH, MODE_CHAT, MODE_MENU };
  enum RxState { RX_IDLE, RX_SEP, RX_SS2, RX_SS2_ACCENT, RX_ESC, RX_PRO2, RX_SKIP };

  enum ConvType : uint8_t { CONV_CHANNEL = 0, CONV_CONTACT = 1 };
  struct Conv {
    uint8_t type;
    uint8_t id[4];   // channel index, or contact public key prefix
  };

  enum MsgFlags : uint8_t { MSG_UNREAD = 1, MSG_OUT = 2, MSG_ACKED = 4, MSG_TIMEOUT = 8 };
  struct Msg {
    Conv conv;
    uint8_t hops;
    int8_t snr4;
    uint8_t flags;
    uint8_t bytes;                 // text size on air
    uint32_t ack;                  // expected ACK of an outgoing private message
    unsigned long ack_deadline;
    char text[MINITEL_MSG_SLOT];   // sanitized "name: text"
  };

  // keyboard events decoded from one batch of received bytes
  struct KeyEvent {
    uint8_t key;     // 0 = text, else function key code
    char text[4];    // UTF-8 character
  };

  MyMesh& _mesh;
  mesh::MainBoard& _board;
  Minitel* _mt = NULL;

  State _state = PROBE_WAIT;
  Mode _mode = MODE_SPLASH;
  unsigned long _deadline = 0;
  unsigned long _next_ping = 0;
  bool _ping_pending = false;
  unsigned long _next_header = 0;
  unsigned long _next_ack_check = 0;
  unsigned long _splash_until = 0;
  volatile bool _rx_error = false;

  RxState _rx = RX_IDLE;
  uint8_t _rx_accent = 0;
  uint8_t _rx_skip = 0;
  uint8_t _pro[2];
  uint8_t _pro_len = 0;
  KeyEvent _events[16];
  int _num_events = 0;

  Msg _msgs[MINITEL_HISTORY];
  int _msg_head = 0;
  int _msg_count = 0;

  Conv _conv;
  int _scroll = 0;        // lines scrolled back from the newest
  bool _details = false;  // show hops / SNR / bytes

  char _input[MINITEL_INPUT_SIZE];
  int _input_len = 0;
  int _input_chars = 0;
  char _input_saved[MINITEL_INPUT_SIZE];   // input at the last confirmed link check

  int _menu_page = 0;
  int _menu_pages = 1;
  char _menu_num[4];
  int _menu_num_len = 0;

  bool _full_dirty = false;
  bool _msgs_dirty = false;
  bool _header_dirty = false;

  void writeRaw(const uint8_t* data, int len);
  void startProbe();
  void goOnline();
  void goOffline();
  void readInput();
  void handleByte(uint8_t b);
  void addEvent(uint8_t key, const char* text);
  void onKey(const KeyEvent& ev);
  void onChatKey(const KeyEvent& ev);
  void onMenuKey(const KeyEvent& ev);
  void onSpeedReply(uint8_t code);
  void checkAckTimeouts();

  Msg& newMsg(const Conv& conv);
  bool sameConv(const Conv& a, const Conv& b) const { return a.type == b.type && memcmp(a.id, b.id, 4) == 0; }
  bool convName(const Conv& conv, char* dest, int dest_size);
  ContactInfo* convContact(const Conv& conv);
  int countUnread(const Conv* conv);
  void openConv(const Conv& conv);

  void typeText(const char* utf8);
  void backspace();
  void clearInput();
  void sendInput();
  int inputMaxBytes();

  int composeMsg(const Msg& m, char* dest, int dest_size);
  int countMsgLines();
  void renderAll();
  void renderSplash();
  void renderHeader();
  void renderMessages();
  void renderHint();
  void renderCounter();
  void renderInput();
  void placeCursor();
  int buildMenu(Conv* dest, int max_entries, int skip);
  void renderMenu();
  void printAt(int col, int row, const char* text, bool inverse = false);
  void printCentered(int row, const char* text, bool inverse = false);
};
