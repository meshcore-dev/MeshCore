#ifdef MINITEL_RX

#include "MinitelUI.h"
#include "MyMesh.h"
#include <time.h>
#include <Minitel1B_Hard.h>   // keep last: defines many short macros (ESC, CR, SEP, ...)

// Screen layout (rows 1..24, 40 columns)
#define COLS           40
#define ROW_HEADER     1
#define ROW_MSG_FIRST  2
#define MSG_ROWS       19
#define ROW_HINT       21
#define ROW_INPUT      22
#define INPUT_ROWS     3
#define PROMPT_COLS    2
#define INPUT_MAX_CHARS  (INPUT_ROWS*COLS - PROMPT_COLS - 1)   // never write the very last cell
#define HINT_COLS      30   // hint text, then the byte counter
#define MENU_ROW_FIRST   3
#define MENU_ROWS        17
#define MENU_ROW_TOOLS   20   // just below the conversation list: entry point to "Autres fonctions"

// reserved menu number for "Autres fonctions": well above any realistic
// conversation count, so it can never collide with a real entry
#define MENU_ACTION_TOOLS  900

#define TOOLS_ROW_ZEROHOP  3
#define TOOLS_ROW_FLOOD    4
#define TOOLS_ROW_OLED     5
#define TOOLS_ROW_STATUS   8

#define MAX_WRAP_LINES 10

#define PROBE_RETRY_MILLIS    5000
#define PROBE_REPLY_MILLIS    1000
#define PING_INTERVAL_MILLIS  10000
#define PING_REPLY_MILLIS     2000
#define HEADER_REFRESH_MILLIS 10000
#define MENU_REFRESH_MILLIS   10000  // how often the battery voltage in the SOMMAIRE header is refreshed
#define SPLASH_MILLIS         30000
#define ACK_MARGIN_MILLIS     2000
#define MAX_AUTORESEND        3     // how many times to auto-resend an unconfirmed private message
#define MSG_LED_MILLIS        150   // how long the optional message LED stays lit

#ifndef MAX_GROUP_CHANNELS
  #define MAX_GROUP_CHANNELS  1
#endif

#ifdef MINITEL_DEBUG
  #define MT_DEBUG(...)  Serial.printf(__VA_ARGS__)
#else
  #define MT_DEBUG(...)  {}
#endif

// Minitel protocol bytes (see 'Spécifications Techniques d'Utilisation du Minitel 1B')
#define MT_ESC   0x1B
#define MT_SEP   0x13
#define MT_SS2   0x19
#define MT_PRO1  0x39
#define MT_PRO2  0x3A
#define MT_PRO3  0x3B
#define MT_CSI   0x5B
#define MT_REP_SPEED   0x75
#define MT_SPEED_4800  0x76

// Function keys (byte following SEP)
#define KEY_TEXT        0
#define KEY_ENVOI       0x41
#define KEY_RETOUR      0x42
#define KEY_REPETITION  0x43
#define KEY_GUIDE       0x44
#define KEY_ANNULATION  0x45
#define KEY_SOMMAIRE    0x46
#define KEY_CORRECTION  0x47
#define KEY_SUITE       0x48

static const uint8_t CMD_QUERY_SPEED[] = { MT_ESC, MT_PRO1, 0x74 };
static const uint8_t CMD_SPEED_4800[]  = { MT_ESC, MT_PRO2, 0x6B, MT_SPEED_4800 };
static const uint8_t CMD_ECHO_OFF[]    = { MT_ESC, MT_PRO3, 0x60, 0x5A, 0x51 };  // keyboard -> modem routing off
static const uint8_t CMD_LOWERCASE[]   = { MT_ESC, MT_PRO2, 0x69, 0x45 };
// Force the Minitel's internal routing ("aiguillage") toward our peripheral socket,
// in case it's stuck on some other routing (eg. from fiddling with its own Fnct+T menu):
// what comes in from the socket shows on screen, and keys typed go out to the socket.
// Sent raw, no wait for an acknowledgement (same fire-and-forget style as CMD_ECHO_OFF):
// a Minitel that's not listening wouldn't answer anyway, and this is harmless if already set.
static const uint8_t CMD_ROUTE_SOCKET_TO_SCREEN[]   = { MT_ESC, MT_PRO3, 0x61, 0x58, 0x53 };
static const uint8_t CMD_ROUTE_KEYBOARD_TO_SOCKET[] = { MT_ESC, MT_PRO3, 0x61, 0x5B, 0x51 };

static bool reached(unsigned long t) { return (long)(millis() - t) >= 0; }

static int utf8Len(uint8_t lead) {
  if (lead < 0x80) return 1;
  if (lead < 0xE0) return 2;
  if (lead < 0xF0) return 3;
  return 4;
}

static int countChars(const char* s, int len) {
  int n = 0;
  for (int i = 0; i < len; i++) {
    if (((uint8_t)s[i] & 0xC0) != 0x80) n++;
  }
  return n;
}

// code points Minitel1B_Hard::print() can render, each one column wide
static const uint16_t DISPLAYABLE[] = {
  0xA3, 0xA7, 0xB0, 0xB1, 0xBC, 0xBD, 0xBE, 0xC0, 0xC2, 0xC4, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCE,
  0xCF, 0xD4, 0xD6, 0xD9, 0xDB, 0xDC, 0xE0, 0xE2, 0xE4, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEE, 0xEF,
  0xF4, 0xF6, 0xF7, 0xF9, 0xFB, 0xFC, 0x152, 0x153, 0x3B2, 0x2014, 0x2190, 0x2191, 0x2192, 0x2193
};

static const struct { uint16_t cp; const char* rep; } REPLACEMENTS[] = {
  { 0xA0, " " }, { 0xAB, "\"" }, { 0xBB, "\"" }, { 0x2013, "-" }, { 0x2018, "'" }, { 0x2019, "'" },
  { 0x201C, "\"" }, { 0x201D, "\"" }, { 0x2026, "..." }, { 0x20AC, "EUR" },
  { 0xC1, "A" }, { 0xC3, "A" }, { 0xC5, "A" }, { 0xCD, "I" }, { 0xD1, "N" }, { 0xD3, "O" }, { 0xDA, "U" },
  { 0xE1, "a" }, { 0xE3, "a" }, { 0xE5, "a" }, { 0xEC, "i" }, { 0xED, "i" }, { 0xF1, "n" }, { 0xF2, "o" },
  { 0xF3, "o" }, { 0xF5, "o" }, { 0xFA, "u" }, { 0xFD, "y" }, { 0xFF, "y" }
};

// Maps one code point to what the Minitel can show. tmp must hold 4 bytes.
static const char* mapCodepoint(uint32_t cp, char* tmp) {
  if (cp < 0x20 || cp == 0x7F || cp == '^') return " ";
  if (cp == '`') return "'";
  if (cp < 0x80) {
    tmp[0] = (char)cp; tmp[1] = 0;
    return tmp;
  }
  for (unsigned i = 0; i < sizeof(DISPLAYABLE)/sizeof(DISPLAYABLE[0]); i++) {
    if (DISPLAYABLE[i] == cp) {
      if (cp < 0x800) {
        tmp[0] = 0xC0 | (cp >> 6); tmp[1] = 0x80 | (cp & 0x3F); tmp[2] = 0;
      } else {
        tmp[0] = 0xE0 | (cp >> 12); tmp[1] = 0x80 | ((cp >> 6) & 0x3F); tmp[2] = 0x80 | (cp & 0x3F); tmp[3] = 0;
      }
      return tmp;
    }
  }
  for (unsigned i = 0; i < sizeof(REPLACEMENTS)/sizeof(REPLACEMENTS[0]); i++) {
    if (REPLACEMENTS[i].cp == cp) return REPLACEMENTS[i].rep;
  }
  return "?";   // emoji, other scripts...
}

// Converts UTF-8 text to the Minitel displayable subset, so that one code point == one column.
static int sanitize(char* dest, int dest_size, const char* src, int src_len) {
  int o = 0;
  int i = 0;
  char tmp[4];
  while (i < src_len && src[i]) {
    uint8_t c = src[i];
    uint32_t cp;
    int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
    else { cp = '?'; n = 1; }   // stray continuation byte
    if (i + n > src_len) break;
    for (int k = 1; k < n; k++) cp = (cp << 6) | (src[i + k] & 0x3F);
    i += n;

    const char* rep = mapCodepoint(cp, tmp);
    int rl = strlen(rep);
    if (o + rl >= dest_size) break;
    memcpy(&dest[o], rep, rl);
    o += rl;
  }
  dest[o] = 0;
  return o;
}

// Truncates sanitized text to max_cols columns, returns the new byte length
static int truncateCols(char* s, int max_cols) {
  int i = 0, cols = 0;
  while (s[i] && cols < max_cols) {
    i += utf8Len(s[i]);
    cols++;
  }
  s[i] = 0;
  return i;
}

// Pads sanitized text with spaces up to cols columns
static int padCols(char* s, int len, int cols, int size) {
  int n = countChars(s, len);
  while (n < cols && len < size - 1) {
    s[len++] = ' ';
    n++;
  }
  s[len] = 0;
  return len;
}

// Word-wraps sanitized text into lines of at most COLS columns
static int wrapLines(const char* s, int len, int* offs, int* lens, int max_lines) {
  int n = 0;
  int start = 0;
  do {
    int i = start, cols = 0, last_space = -1;
    while (i < len && cols < COLS) {
      if (s[i] == ' ') last_space = i;
      i += utf8Len(s[i]);
      cols++;
    }
    int end = i;
    if (i < len && s[i] != ' ' && last_space > start) end = last_space;   // break at the last space
    offs[n] = start;
    lens[n] = end - start;
    n++;
    start = end;
    while (start < len && s[start] == ' ') start++;
  } while (start < len && n < max_lines);
  return n;
}

// Minitel keyboard accent sequences: SS2 <accent> <letter>
static const char* composeAccent(uint8_t accent, uint8_t letter) {
  switch (accent) {
    case 0x41:  // grave
      if (letter == 'a') return "à";
      if (letter == 'e') return "è";
      if (letter == 'u') return "ù";
      break;
    case 0x42:  // acute
      if (letter == 'e') return "é";
      break;
    case 0x43:  // circumflex
      if (letter == 'a') return "â";
      if (letter == 'e') return "ê";
      if (letter == 'i') return "î";
      if (letter == 'o') return "ô";
      if (letter == 'u') return "û";
      break;
    case 0x48:  // diaeresis
      if (letter == 'a') return "ä";
      if (letter == 'e') return "ë";
      if (letter == 'i') return "ï";
      if (letter == 'o') return "ö";
      if (letter == 'u') return "ü";
      break;
    case 0x4B:  // cedilla
      if (letter == 'c') return "ç";
      break;
  }
  return NULL;
}

static const char* g2Char(uint8_t b) {
  switch (b) {
    case 0x23: return "£";
    case 0x27: return "§";
    case 0x30: return "°";
    case 0x31: return "±";
    case 0x38: return "÷";
    case 0x3C: return "¼";
    case 0x3D: return "½";
    case 0x3E: return "¾";
    case 0x6A: return "Œ";
    case 0x7A: return "œ";
  }
  return NULL;
}

/* ------------------------------------------------------------------------------------ */

// The library writes one byte at a time, and the ESP32 UART driver stores each write in its
// TX ring buffer with a large overhead, so a screen update would block the mesh loop.
// Writes are queued in RAM instead, and pump() moves them to the UART FIFO without waiting.
class MinitelSerial : public HardwareSerial {
public:
  MinitelSerial(int uart_nr) : HardwareSerial(uart_nr) { }

  size_t write(uint8_t c) override {
    while (_count == sizeof(_q)) pump();   // queue full: wait for the UART (only if a screen update was too big)
    _q[_head] = c;
    _head = (_head + 1) % sizeof(_q);
    _count++;
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    for (size_t i = 0; i < size; i++) write(buf[i]);
    return size;
  }
  void flush() {
    while (_count > 0) pump();
    HardwareSerial::flush();
  }
  void pump() {
    while (_count > 0) {
      int room = HardwareSerial::availableForWrite();
      if (room <= 0) return;
      int chunk = sizeof(_q) - _tail;   // contiguous bytes
      if (chunk > _count) chunk = _count;
      if (chunk > room) chunk = room;
      HardwareSerial::write(&_q[_tail], chunk);
      _tail = (_tail + chunk) % sizeof(_q);
      _count -= chunk;
    }
  }
  int queueFree() const { return sizeof(_q) - _count; }
  void discard() { _head = _tail = _count = 0; }

private:
  uint8_t _q[4096];
  int _head = 0;
  int _tail = 0;
  int _count = 0;
};

static MinitelSerial mt_serial(MINITEL_UART);

void MinitelUI::begin() {
  _conv.type = CONV_CHANNEL;
  memset(_conv.id, 0, sizeof(_conv.id));
  _conv.id[0] = MINITEL_CHANNEL_IDX;
  _input[0] = _input_saved[0] = 0;

#ifdef MSG_LED_PIN
  pinMode(MSG_LED_PIN, OUTPUT);
  digitalWrite(MSG_LED_PIN, LOW);
#endif

  // parity / framing errors: noise on the line, or the Minitel is not at our speed
  mt_serial.onReceiveError([this](hardwareSerial_error_t err) { onRxError(); });
  _mt = new Minitel(mt_serial, MINITEL_RX, MINITEL_TX);   // starts the port at 1200 bauds 7E1
  _state = PROBE_WAIT;
  _deadline = millis();
}

void MinitelUI::writeRaw(const uint8_t* data, int len) {
  for (int i = 0; i < len; i++) _mt->writeByte(data[i]);
}

// Optional LED (reuses the radio's existing TX-indicator pin, see MSG_LED_PIN):
// flashes briefly on every received message, same trigger as the Minitel's own
// bip(). The same LED already flashes on every transmission too (unrelated,
// existing code: ESP32Board::onBeforeTransmit/onAfterTransmit), so it now
// indicates both directions. No-op if the board doesn't define MSG_LED_PIN.
void MinitelUI::flashMsgLed() {
#ifdef MSG_LED_PIN
  digitalWrite(MSG_LED_PIN, HIGH);
  _msg_led_until = millis() + MSG_LED_MILLIS;
#endif
}

// Step 1: maybe the Minitel is already at 4800 (ESP restarted, Minitel kept on)
void MinitelUI::startProbe() {
  mt_serial.updateBaudRate(4800);
  writeRaw(CMD_ROUTE_SOCKET_TO_SCREEN, sizeof(CMD_ROUTE_SOCKET_TO_SCREEN));
  writeRaw(CMD_ROUTE_KEYBOARD_TO_SOCKET, sizeof(CMD_ROUTE_KEYBOARD_TO_SOCKET));
  writeRaw(CMD_QUERY_SPEED, sizeof(CMD_QUERY_SPEED));
  _state = PROBE_QUERY;
  _deadline = millis() + PROBE_REPLY_MILLIS;
}

void MinitelUI::goOnline() {
  MT_DEBUG("[MT] online\n");
  _state = ONLINE;
  _ping_pending = false;
  _next_ping = millis() + PING_INTERVAL_MILLIS;
  writeRaw(CMD_ECHO_OFF, sizeof(CMD_ECHO_OFF));
  writeRaw(CMD_LOWERCASE, sizeof(CMD_LOWERCASE));
  _mode = MODE_SPLASH;
  _splash_until = millis() + SPLASH_MILLIS;
  _full_dirty = true;
}

void MinitelUI::goOffline() {
  MT_DEBUG("[MT] offline\n");
  mt_serial.discard();   // pending screen data is useless now
  // keys received since the last good link check may be garbage (Minitel restarted at 1200 bauds)
  strcpy(_input, _input_saved);
  _input_len = strlen(_input);
  _input_chars = countChars(_input, _input_len);
  _state = PROBE_WAIT;
  _deadline = millis() + PROBE_RETRY_MILLIS;
}

void MinitelUI::onSpeedReply(uint8_t code) {
  if (code != MT_SPEED_4800) return;
  if (_state == ONLINE) {
    _ping_pending = false;
    strcpy(_input_saved, _input);
  } else {
    goOnline();
  }
}

void MinitelUI::loop() {
  if (_mt == NULL) return;

  mt_serial.pump();
  readInput();

  switch (_state) {
    case PROBE_WAIT:
      if (reached(_deadline)) startProbe();
      return;
    case PROBE_QUERY:
      if (reached(_deadline)) {
        // Step 2: Minitel just powered on (1200 bauds): ask it to switch to 4800
        mt_serial.updateBaudRate(1200);
        writeRaw(CMD_ROUTE_SOCKET_TO_SCREEN, sizeof(CMD_ROUTE_SOCKET_TO_SCREEN));
        writeRaw(CMD_ROUTE_KEYBOARD_TO_SOCKET, sizeof(CMD_ROUTE_KEYBOARD_TO_SOCKET));
        writeRaw(CMD_SPEED_4800, sizeof(CMD_SPEED_4800));
        mt_serial.flush();   // ~35ms, only while no Minitel answers
        mt_serial.updateBaudRate(4800);
        _state = PROBE_SWITCH;
        _deadline = millis() + PROBE_REPLY_MILLIS;
      }
      return;
    case PROBE_SWITCH:
      if (reached(_deadline)) goOffline();
      return;
    case ONLINE:
      break;
  }

  // keep-alive: detects the Minitel being switched off (it comes back at 1200 bauds)
  if (_ping_pending) {
    if (reached(_deadline)) {
      goOffline();
      return;
    }
  } else if (reached(_next_ping)) {
    writeRaw(CMD_QUERY_SPEED, sizeof(CMD_QUERY_SPEED));
    _ping_pending = true;
    _deadline = millis() + PING_REPLY_MILLIS;
    _next_ping = millis() + PING_INTERVAL_MILLIS;
  }

  if (reached(_next_header)) {
    _header_dirty = true;
    _next_header = millis() + HEADER_REFRESH_MILLIS;
  }
  if (reached(_next_ack_check)) {
    checkAckTimeouts();
    _next_ack_check = millis() + 1000;
  }
#ifdef MSG_LED_PIN
  if (_msg_led_until && reached(_msg_led_until)) {
    digitalWrite(MSG_LED_PIN, LOW);
    _msg_led_until = 0;
  }
#endif
  if (_mode == MODE_SPLASH && !_full_dirty && reached(_splash_until)) {
    _mode = needsPairing() ? MODE_PAIRING : MODE_CHAT;
    _full_dirty = true;
  }
  if (_mode == MODE_PAIRING && _mesh.isBLEConnected()) {
    _mode = MODE_CHAT;
    _full_dirty = true;
  }
  if (_mode == MODE_MENU && !_full_dirty && reached(_next_menu_refresh)) {
    _full_dirty = true;   // refresh the battery voltage in the SOMMAIRE header
    _next_menu_refresh = millis() + MENU_REFRESH_MILLIS;
  }

  // only draw when the TX queue has room for a whole update
  int free_tx = mt_serial.queueFree();
  if (_full_dirty) {
    if (free_tx > 3000) renderAll();
  } else if (_mode == MODE_CHAT) {
    if (_msgs_dirty && free_tx > 2500) {
      renderMessages();
      renderHint();
      _msgs_dirty = false;
      if (_header_dirty) renderHeader();
      placeCursor();
    } else if (_header_dirty && free_tx > 200) {
      renderHeader();
      placeCursor();
    }
  }
  mt_serial.pump();
}

/* ------------------------------------------------------------------------------------ */

void MinitelUI::readInput() {
  uint8_t batch[64];
  int n = 0;
  while (n < (int)sizeof(batch) && mt_serial.available() > 0) {
    batch[n++] = mt_serial.read() & 0x7F;
  }
  if (n == 0) return;

  bool err = _rx_error;
  _rx_error = false;
  _num_events = 0;
  for (int i = 0; i < n; i++) {
    MT_DEBUG("%02X ", batch[i]);
    handleByte(batch[i]);
  }
  MT_DEBUG(err ? "<- rx error\n" : "\n");
  if (_state != ONLINE) return;

  // A person types one key at a time: several characters in the same batch, or a line error,
  // is noise (or a Minitel that restarted at another speed). Drop the keys and check the link now.
  int typed = 0;
  for (int i = 0; i < _num_events; i++) {
    if (_events[i].key == KEY_TEXT) typed++;
  }
  if (err || typed > 1) {
    MT_DEBUG("[MT] dropped %d key(s)\n", _num_events);
    if (!_ping_pending) _next_ping = millis();
    return;
  }
  for (int i = 0; i < _num_events; i++) onKey(_events[i]);
}

void MinitelUI::addEvent(uint8_t key, const char* text) {
  if (_state != ONLINE || _num_events >= (int)(sizeof(_events) / sizeof(_events[0]))) return;
  KeyEvent& ev = _events[_num_events++];
  ev.key = key;
  StrHelper::strncpy(ev.text, text ? text : "", sizeof(ev.text));
}

void MinitelUI::handleByte(uint8_t b) {
  // resynchronise on a new sequence, whatever the current state
  if (_rx != RX_PRO2 && _rx != RX_SKIP) {
    if (b == MT_ESC) { _rx = RX_ESC; return; }
    if (b == MT_SEP) { _rx = RX_SEP; return; }
  }
  switch (_rx) {
    case RX_IDLE:
      if (b == MT_SS2) _rx = RX_SS2;
      else if (b >= 0x20 && b < 0x7F) {
        char s[2] = { (char)b, 0 };
        addEvent(KEY_TEXT, s);
      }
      break;
    case RX_SEP:
      _rx = RX_IDLE;
      addEvent(b, NULL);
      break;
    case RX_SS2:
      if (b == 0x41 || b == 0x42 || b == 0x43 || b == 0x48 || b == 0x4B) {
        _rx_accent = b;
        _rx = RX_SS2_ACCENT;
      } else {
        _rx = RX_IDLE;
        const char* s = g2Char(b);
        if (s) addEvent(KEY_TEXT, s);
      }
      break;
    case RX_SS2_ACCENT:
      if (b == MT_SS2) {   // accent key pressed again
        _rx = RX_SS2;
      } else {
        _rx = RX_IDLE;
        const char* s = composeAccent(_rx_accent, b);
        if (s) addEvent(KEY_TEXT, s);
      }
      break;
    case RX_ESC:
      if (b == MT_PRO2) {   // ESC 0x3A <status> <value>: speed replies
        _pro_len = 0;
        _rx = RX_PRO2;
      } else if (b == MT_PRO3) {
        _rx_skip = 3;
        _rx = RX_SKIP;
      } else if (b == MT_PRO1 || b == MT_CSI) {
        _rx_skip = 1;
        _rx = RX_SKIP;
      } else {
        _rx = RX_IDLE;
      }
      break;
    case RX_PRO2:
      _pro[_pro_len++] = b;
      if (_pro_len == 2) {
        _rx = RX_IDLE;
        if (_pro[0] == MT_REP_SPEED) onSpeedReply(_pro[1]);
      }
      break;
    case RX_SKIP:
      if (--_rx_skip == 0) _rx = RX_IDLE;
      break;
  }
}

void MinitelUI::onKey(const KeyEvent& ev) {
  if (ev.key == KEY_REPETITION) {
    _full_dirty = true;
    return;
  }
  switch (_mode) {
    case MODE_SPLASH:
      _mode = needsPairing() ? MODE_PAIRING : MODE_CHAT;
      _full_dirty = true;
      break;
    case MODE_PAIRING:
      _mode = MODE_CHAT;
      _full_dirty = true;
      break;
    case MODE_CHAT:
      onChatKey(ev);
      break;
    case MODE_MENU:
      onMenuKey(ev);
      break;
    case MODE_TOOLS:
      onToolsKey(ev);
      break;
  }
}

bool MinitelUI::needsPairing() const {
  return _mesh.getBLEPin() != 0 && !_mesh.isBLEConnected();
}

void MinitelUI::onChatKey(const KeyEvent& ev) {
  switch (ev.key) {
    case KEY_TEXT:
      typeText(ev.text);
      break;
    case KEY_ENVOI:
      sendInput();
      break;
    case KEY_CORRECTION:
      backspace();
      break;
    case KEY_ANNULATION:
      if (_input_len == 0) _mt->bip();
      clearInput();
      break;
    case KEY_RETOUR: {   // older messages
      int max_scroll = countMsgLines() - MSG_ROWS;
      if (_scroll >= max_scroll) {
        _mt->bip();
      } else {
        _scroll += MSG_ROWS - 1;
        _msgs_dirty = true;
      }
      break;
    }
    case KEY_SUITE:      // newer messages
      if (_scroll == 0) {
        _mt->bip();
      } else {
        _scroll -= MSG_ROWS - 1;
        if (_scroll < 0) _scroll = 0;
        _msgs_dirty = true;
      }
      break;
    case KEY_GUIDE:
      _details = !_details;
      _msgs_dirty = true;
      break;
    case KEY_SOMMAIRE:
      _mode = MODE_MENU;
      _menu_page = 0;
      _menu_num_len = 0;
      _menu_status[0] = 0;
      _next_menu_refresh = millis() + MENU_REFRESH_MILLIS;
      _full_dirty = true;
      break;
  }
}

void MinitelUI::onMenuKey(const KeyEvent& ev) {
  switch (ev.key) {
    case KEY_TEXT:
      if (ev.text[0] >= '0' && ev.text[0] <= '9' && _menu_num_len < 3) {
        _menu_num[_menu_num_len++] = ev.text[0];
        _menu_num[_menu_num_len] = 0;
        _mt->print(String(ev.text));
      } else {
        _mt->bip();
      }
      break;
    case KEY_CORRECTION:
      if (_menu_num_len > 0) {
        _menu_num[--_menu_num_len] = 0;
        _mt->moveCursorLeft(1);
        _mt->printChar(' ');
        _mt->moveCursorLeft(1);
      } else {
        _mt->bip();
      }
      break;
    case KEY_ENVOI: {
      int num = _menu_num_len > 0 ? atoi(_menu_num) : 0;
      if (num == MENU_ACTION_TOOLS) {
        _mode = MODE_TOOLS;
        _menu_num_len = 0;
        _menu_status[0] = 0;
        _full_dirty = true;
        break;
      }
      Conv conv;
      if (num >= 1 && num <= buildMenu(&conv, 1, num - 1)) {
        openConv(conv);
      } else {
        _mt->bip();
        _menu_num_len = 0;
        _full_dirty = true;
      }
      break;
    }
    case KEY_SUITE:
      if (_menu_page + 1 < _menu_pages) {
        _menu_page++;
        _full_dirty = true;
      } else {
        _mt->bip();
      }
      break;
    case KEY_RETOUR:
      if (_menu_page > 0) {
        _menu_page--;
        _full_dirty = true;
      } else {
        _mt->bip();
      }
      break;
    case KEY_SOMMAIRE:
    case KEY_ANNULATION:
      _mode = MODE_CHAT;
      _full_dirty = true;
      break;
  }
}

// "Autres fonctions" : zero-hop / flood adverts, OLED toggle. Reached from the
// SOMMAIRE (number 900), returns to it rather than straight to the conversation.
void MinitelUI::onToolsKey(const KeyEvent& ev) {
  switch (ev.key) {
    case KEY_TEXT:
      if (ev.text[0] >= '0' && ev.text[0] <= '9' && _menu_num_len < 3) {
        _menu_num[_menu_num_len++] = ev.text[0];
        _menu_num[_menu_num_len] = 0;
        _mt->print(String(ev.text));
      } else {
        _mt->bip();
      }
      break;
    case KEY_CORRECTION:
      if (_menu_num_len > 0) {
        _menu_num[--_menu_num_len] = 0;
        _mt->moveCursorLeft(1);
        _mt->printChar(' ');
        _mt->moveCursorLeft(1);
      } else {
        _mt->bip();
      }
      break;
    case KEY_ENVOI: {
      int num = _menu_num_len > 0 ? atoi(_menu_num) : 0;
      if (num == 1 || num == 2) {
        bool zerohop = (num == 1);
        bool ok = zerohop ? _mesh.advert() : _mesh.advertFlood();
        strcpy(_menu_status, ok
          ? (zerohop ? "Annonce envoyée (voisins)." : "Annonce envoyée (tout le réseau).")
          : "Échec de l'envoi de l'annonce.");
      } else if (num == 3 && _display) {
        _oled_always_on = !_oled_always_on;
        // keepAwake() first: makes the OLED driver's own turnOff() a no-op from
        // now on, instead of racing against ui-new/UITask.cpp's auto-off timer
        // every loop() (that fight was the cause of the flicker/artifacts).
        _display->keepAwake(_oled_always_on);
        if (_oled_always_on) _display->turnOn();
        _display->dim(_oled_always_on);
        strcpy(_menu_status, _oled_always_on
          ? "Écran OLED : toujours allumé (faible)."
          : "Écran OLED : normal.");
      } else {
        _mt->bip();
      }
      _menu_num_len = 0;
      _full_dirty = true;
      break;
    }
    case KEY_SOMMAIRE:
    case KEY_ANNULATION:
      _mode = MODE_MENU;
      _menu_status[0] = 0;
      _full_dirty = true;
      break;
  }
}

/* ------------------------------------------------------------------------------------ */

MinitelUI::Msg& MinitelUI::newMsg(const Conv& conv) {
  Msg& m = _msgs[_msg_head];
  _msg_head = (_msg_head + 1) % MINITEL_HISTORY;
  if (_msg_count < MINITEL_HISTORY) _msg_count++;
  memset(&m, 0, sizeof(m));
  m.conv = conv;
  m.hops = MINITEL_HOPS_LOCAL;
  if (!sameConv(conv, _conv)) {
    m.flags = MSG_UNREAD;
    _header_dirty = true;
  }
  if (_mode == MODE_MENU) _full_dirty = true;   // unread counters
  _msgs_dirty = true;
  return m;
}

// message n, from the oldest (0) to the newest (_msg_count - 1)
#define MSG_AT(n)  _msgs[(_msg_head - _msg_count + (n) + MINITEL_HISTORY) % MINITEL_HISTORY]

void MinitelUI::onChannelMessage(uint8_t channel_idx, const char* sender, const char* text, int text_len,
                                 uint8_t hops, int8_t snr4, uint32_t timestamp) {
  Conv conv = { CONV_CHANNEL, { channel_idx, 0, 0, 0 } };

  char raw[MINITEL_MSG_SLOT + 40];
  int len = 0;
  if (sender) {
    len = snprintf(raw, sizeof(raw), "%s: ", sender);
  }
  if (text_len > (int)sizeof(raw) - 1 - len) text_len = sizeof(raw) - 1 - len;
  memcpy(&raw[len], text, text_len);
  len += text_len;
  raw[len] = 0;

  Msg& m = newMsg(conv);
  m.hops = hops;
  m.snr4 = snr4;
  m.bytes = len;
  m.timestamp = timestamp ? timestamp : _mesh.getRTCClock()->getCurrentTime();
  if (sender) m.flags = MSG_OUT;   // our own message is never unread
  sanitize(m.text, sizeof(m.text), raw, len);

  if (sender == NULL && _mt && _state == ONLINE) _mt->bip();
  if (sender == NULL) flashMsgLed();
}

void MinitelUI::onDirectMessage(const ContactInfo& from, const char* text, uint8_t hops, int8_t snr4, uint32_t timestamp) {
  Conv conv = { CONV_CONTACT };
  memcpy(conv.id, from.id.pub_key, 4);

  char raw[MINITEL_MSG_SLOT + 40];
  snprintf(raw, sizeof(raw), "%s: %s", from.name, text);

  Msg& m = newMsg(conv);
  m.hops = hops;
  m.snr4 = snr4;
  m.bytes = strlen(text);
  m.timestamp = timestamp ? timestamp : _mesh.getRTCClock()->getCurrentTime();
  sanitize(m.text, sizeof(m.text), raw, strlen(raw));

  if (_mt && _state == ONLINE) _mt->bip();
  flashMsgLed();
}

void MinitelUI::onDirectSent(const ContactInfo& to, const char* text, uint32_t expected_ack, uint32_t est_timeout,
                              bool auto_resend) {
  Conv conv = { CONV_CONTACT };
  memcpy(conv.id, to.id.pub_key, 4);

  char raw[MINITEL_MSG_SLOT + 40];
  snprintf(raw, sizeof(raw), "%s: %s", _mesh.getNodeName(), text);
  char clean[MINITEL_MSG_SLOT];
  sanitize(clean, sizeof(clean), raw, strlen(raw));

  // the app retries unconfirmed messages: update the existing line instead of adding one
  Msg* m = NULL;
  for (int n = _msg_count - 1; n >= 0 && m == NULL; n--) {
    Msg& old = MSG_AT(n);
    if (sameConv(old.conv, conv) && (old.flags & MSG_OUT) && !(old.flags & MSG_ACKED) && strcmp(old.text, clean) == 0) {
      m = &old;
    }
  }
  if (m == NULL) {
    m = &newMsg(conv);
    m->flags = MSG_OUT;
    m->bytes = strlen(text);
    m->timestamp = _mesh.getRTCClock()->getCurrentTime();
    strcpy(m->text, clean);
  }
  m->flags &= ~MSG_TIMEOUT;
  if (auto_resend) m->flags |= MSG_AUTORESEND; else m->flags &= ~MSG_AUTORESEND;
  m->resend_count = 0;
  m->ack = expected_ack;
  m->ack_deadline = millis() + est_timeout + ACK_MARGIN_MILLIS;
  if (sameConv(conv, _conv)) _msgs_dirty = true;
}

void MinitelUI::onAck(uint32_t ack) {
  for (int n = 0; n < _msg_count; n++) {
    Msg& m = MSG_AT(n);
    if ((m.flags & MSG_OUT) && m.ack == ack) {
      m.flags |= MSG_ACKED;
      m.flags &= ~MSG_TIMEOUT;
      if (sameConv(m.conv, _conv)) _msgs_dirty = true;
    }
  }
}

void MinitelUI::checkAckTimeouts() {
  for (int n = 0; n < _msg_count; n++) {
    Msg& m = MSG_AT(n);
    if (!(m.ack && !(m.flags & (MSG_ACKED | MSG_TIMEOUT)) && reached(m.ack_deadline))) continue;

    bool resent = false;
    if ((m.flags & MSG_AUTORESEND) && m.resend_count < MAX_AUTORESEND) {
      int prefix_len = strlen(_mesh.getNodeName()) + 2;   // "<name>: " built in onDirectSent()
      ContactInfo* contact = convContact(m.conv);
      if (contact && (int)strlen(m.text) > prefix_len) {
        uint32_t new_ack, new_timeout;
        if (_mesh.sendDirectText(contact, &m.text[prefix_len], new_ack, new_timeout)) {
          m.ack = new_ack;
          m.ack_deadline = millis() + new_timeout + ACK_MARGIN_MILLIS;
          m.resend_count++;
          resent = true;
        }
      }
    }
    if (!resent) m.flags |= MSG_TIMEOUT;
    if (sameConv(m.conv, _conv)) _msgs_dirty = true;
  }
}

ContactInfo* MinitelUI::convContact(const Conv& conv) {
  if (conv.type != CONV_CONTACT) return NULL;
  return _mesh.lookupContactByPubKey(conv.id, 4);
}

bool MinitelUI::convName(const Conv& conv, char* dest, int dest_size) {
  if (conv.type == CONV_CHANNEL) {
    ChannelDetails ch;
    if (_mesh.getChannel(conv.id[0], ch) && ch.name[0]) {
      snprintf(dest, dest_size, "#%s", ch.name);
      return true;
    }
    snprintf(dest, dest_size, "#%d", conv.id[0]);
    return false;
  }
  ContactInfo* c = convContact(conv);
  if (c) {
    snprintf(dest, dest_size, "@%s", c->name);
    return true;
  }
  snprintf(dest, dest_size, "@%02X%02X%02X%02X", conv.id[0], conv.id[1], conv.id[2], conv.id[3]);
  return false;
}

// unread messages of one conversation, or of all of them (conv == NULL)
int MinitelUI::countUnread(const Conv* conv) {
  int n = 0;
  for (int i = 0; i < _msg_count; i++) {
    Msg& m = MSG_AT(i);
    if ((m.flags & MSG_UNREAD) && (conv == NULL || sameConv(m.conv, *conv))) n++;
  }
  return n;
}

void MinitelUI::openConv(const Conv& conv) {
  _conv = conv;
  for (int i = 0; i < _msg_count; i++) {
    Msg& m = MSG_AT(i);
    if (sameConv(m.conv, conv)) m.flags &= ~MSG_UNREAD;
  }
  _scroll = 0;
  // the input in progress is kept as-is across a conversation switch (not cleared)
  _mode = MODE_CHAT;
  _full_dirty = true;
}

/* ------------------------------------------------------------------------------------ */

int MinitelUI::inputMaxBytes() {
  int max_bytes = MAX_TEXT_LEN;
  if (_conv.type == CONV_CHANNEL) max_bytes -= strlen(_mesh.getNodeName()) + 2;   // sent as "<name>: <text>"
  if (max_bytes > MINITEL_INPUT_SIZE - 1) max_bytes = MINITEL_INPUT_SIZE - 1;
  return max_bytes;
}

void MinitelUI::typeText(const char* utf8) {
  int len = strlen(utf8);
  if (_input_chars >= INPUT_MAX_CHARS || _input_len + len > inputMaxBytes()) {
    _mt->bip();
    return;
  }
  int pos = PROMPT_COLS + _input_chars;
  memcpy(&_input[_input_len], utf8, len);
  _input_len += len;
  _input[_input_len] = 0;
  _input_chars++;

  _mt->moveCursorXY(1 + pos % COLS, ROW_INPUT + pos / COLS);
  _mt->print(String(utf8));
  renderCounter();
  placeCursor();
}

void MinitelUI::backspace() {
  if (_input_len == 0) {
    _mt->bip();
    return;
  }
  do {
    _input_len--;
  } while (_input_len > 0 && ((uint8_t)_input[_input_len] & 0xC0) == 0x80);
  _input[_input_len] = 0;
  _input_chars--;

  int pos = PROMPT_COLS + _input_chars;
  _mt->moveCursorXY(1 + pos % COLS, ROW_INPUT + pos / COLS);
  _mt->printChar(' ');
  renderCounter();
  placeCursor();
}

void MinitelUI::clearInput() {
  _input_len = _input_chars = 0;
  _input[0] = _input_saved[0] = 0;
  if (_state == ONLINE && _mode == MODE_CHAT) {
    renderInput();
    renderCounter();
    placeCursor();
  }
}

void MinitelUI::sendInput() {
  if (_input_len == 0) {
    _mt->bip();
    return;
  }

  bool sent = false;
  if (_conv.type == CONV_CHANNEL) {
    ChannelDetails ch;
    uint32_t timestamp = _mesh.getRTCClock()->getCurrentTimeUnique();
    if (_mesh.getChannel(_conv.id[0], ch)
        && _mesh.sendGroupMessage(timestamp, ch.channel, _mesh.getNodeName(), _input, _input_len)) {
      onChannelMessage(_conv.id[0], _mesh.getNodeName(), _input, _input_len);
      // also show it in the companion app
      char full[MAX_TEXT_LEN + 40];
      snprintf(full, sizeof(full), "%s: %s", _mesh.getNodeName(), _input);
      _mesh.pushChannelMsgToApp(_conv.id[0], timestamp, full);
      sent = true;
    }
  } else {
    ContactInfo* contact = convContact(_conv);
    uint32_t expected_ack = 0, est_timeout = 0;
    if (contact && _mesh.sendDirectText(contact, _input, expected_ack, est_timeout)) {
      onDirectSent(*contact, _input, expected_ack, est_timeout, true);
      sent = true;
    }
  }
  if (!sent) {
    _mt->bip();
    return;
  }
  _scroll = 0;
  clearInput();
}

/* ------------------------------------------------------------------------------------ */

// Text shown for a message: "name: text", with the delivery status and the details
int MinitelUI::composeMsg(const Msg& m, char* dest, int dest_size) {
  int len = snprintf(dest, dest_size, "%s", m.text);
  char info[64];
  int il = 0;
  info[0] = 0;
  if (m.ack) {
    const char* status = (m.flags & MSG_ACKED) ? "reçu" : (m.flags & MSG_TIMEOUT) ? "non confirmé" : "...";
    il += snprintf(&info[il], sizeof(info) - il, "%s", status);
  }
  if (_details) {
    time_t t = (time_t)m.timestamp;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    il += snprintf(&info[il], sizeof(info) - il, "%s%02d:%02d", il ? ", " : "", tmv.tm_hour, tmv.tm_min);

    if (!(m.flags & MSG_OUT)) {
      if (m.hops == MINITEL_HOPS_DIRECT) {
        il += snprintf(&info[il], sizeof(info) - il, "%sroute directe", il ? ", " : "");
      } else {
        il += snprintf(&info[il], sizeof(info) - il, "%s%d saut%s", il ? ", " : "", m.hops, m.hops > 1 ? "s" : "");
      }
      int snr = m.snr4 >= 0 ? (m.snr4 + 2) / 4 : (m.snr4 - 2) / 4;
      il += snprintf(&info[il], sizeof(info) - il, ", SNR %d dB", snr);
    }
    il += snprintf(&info[il], sizeof(info) - il, "%s%d o", il ? ", " : "", m.bytes);
  }
  if (il > 0 && len < dest_size - 1) {
    len += snprintf(&dest[len], dest_size - len, " [%s]", info);
    if (len >= dest_size) len = dest_size - 1;
  }
  return len;
}

int MinitelUI::countMsgLines() {
  char buf[MINITEL_MSG_SLOT + 64];
  int offs[MAX_WRAP_LINES], lens[MAX_WRAP_LINES];
  int total = 0;
  for (int n = 0; n < _msg_count; n++) {
    Msg& m = MSG_AT(n);
    if (!sameConv(m.conv, _conv)) continue;
    int len = composeMsg(m, buf, sizeof(buf));
    total += wrapLines(buf, len, offs, lens, MAX_WRAP_LINES);
  }
  return total;
}

// attributes are set after positioning the cursor
void MinitelUI::printAt(int col, int row, const char* text, bool inverse) {
  _mt->moveCursorXY(col, row);
  if (inverse) _mt->attributs(INVERSION_FOND);
  _mt->print(String(text));
  if (inverse) _mt->attributs(FOND_NORMAL);
}

void MinitelUI::printCentered(int row, const char* text, bool inverse) {
  char line[COLS * 3];
  sanitize(line, sizeof(line), text, strlen(text));
  int len = truncateCols(line, COLS);
  int col = 1 + (COLS - countChars(line, len)) / 2;
  printAt(col, row, line, inverse);
}

void MinitelUI::renderAll() {
  switch (_mode) {
    case MODE_SPLASH:
      renderSplash();
      break;
    case MODE_PAIRING:
      renderPairing();
      break;
    case MODE_CHAT:
      _mt->newScreen();
      _mt->cursor();
      renderHeader();
      renderMessages();
      renderHint();
      renderInput();
      placeCursor();
      break;
    case MODE_MENU:
      renderMenu();
      break;
    case MODE_TOOLS:
      renderTools();
      break;
  }
  _full_dirty = _msgs_dirty = _header_dirty = false;
}

void MinitelUI::renderSplash() {
  _mt->newScreen();
  _mt->noCursor();
  _mt->moveCursorXY(13, 3);
  _mt->attributs(DOUBLE_GRANDEUR);   // 8 letters, double width: 16 columns
  _mt->print("MeshCore");
  _mt->attributs(GRANDEUR_NORMALE);
  printCentered(6, "Messagerie LoRa sur Minitel");

  printCentered(9, "Une réalisation de");
  printCentered(10, " Capt_Pierrot & Jonathan ", true);

  printCentered(13, "Basé sur :");
  printCentered(14, "MeshCore - github.com/meshcore-dev");
  printCentered(16, "Minitel1B_Hard - Eric Sérandour");
  printCentered(17, "github.com/eserandour/Minitel1B_Hard");

  char line[80];
  snprintf(line, sizeof(line), "Firmware %s - %s", FIRMWARE_VERSION, _mesh.getNodeName());
  printCentered(20, line);
  printCentered(23, "Appuyez sur une touche");
}

// Shown after the splash screen while no phone has connected over Bluetooth yet
void MinitelUI::renderPairing() {
  _mt->newScreen();
  _mt->noCursor();
  _mt->moveCursorXY(10, 3);
  _mt->attributs(DOUBLE_GRANDEUR);
  _mt->print("Bluetooth");
  _mt->attributs(GRANDEUR_NORMALE);
  printCentered(6, "En attente de l'application MeshCore");

  char line[40];
  printCentered(9, "Nom de l'appareil :");
  snprintf(line, sizeof(line), "%s%s", BLE_NAME_PREFIX, _mesh.getNodeName());
  printCentered(10, line, true);

  printCentered(13, "Code d'appairage :");
  snprintf(line, sizeof(line), "%06u", (unsigned)_mesh.getBLEPin());
  printCentered(14, line, true);

  printCentered(17, "Dans l'application, ajoutez cet");
  printCentered(18, "appareil puis entrez ce code.");
  printCentered(21, "L'écran passe à la conversation");
  printCentered(22, "dès la connexion.");
  printCentered(23, "Appuyez sur une touche pour l'ignorer");
}

void MinitelUI::renderHeader() {
  char raw[64];
  convName(_conv, raw, sizeof(raw));
  char name[COLS * 3];
  sanitize(name, sizeof(name), raw, strlen(raw));

  char right[32];
  int unread = countUnread(NULL);
  int rl = 0;
  if (unread > 0) rl = snprintf(right, sizeof(right), "%d non lu%s  ", unread, unread > 1 ? "s" : "");
  snprintf(&right[rl], sizeof(right) - rl, "%.2fV ", _board.getBattMilliVolts() / 1000.0f);

  char line[COLS * 3 + 4];
  line[0] = ' ';
  strcpy(&line[1], name);
  int len = truncateCols(line, COLS - strlen(right) - 1);
  len = padCols(line, len, COLS - strlen(right), sizeof(line));
  strcpy(&line[len], right);

  _mt->moveCursorXY(1, ROW_HEADER);
  _mt->attributs(INVERSION_FOND);
  _mt->print(String(line));
  _mt->attributs(FOND_NORMAL);
  _header_dirty = false;
}

void MinitelUI::renderMessages() {
  int total = countMsgLines();
  int max_scroll = total > MSG_ROWS ? total - MSG_ROWS : 0;
  if (_scroll > max_scroll) _scroll = max_scroll;

  char buf[MINITEL_MSG_SLOT + 64];
  int offs[MAX_WRAP_LINES], lens[MAX_WRAP_LINES];
  int row = ROW_MSG_FIRST + MSG_ROWS - 1;   // fill from the bottom
  int skip = _scroll;
  for (int n = _msg_count - 1; n >= 0 && row >= ROW_MSG_FIRST; n--) {
    Msg& m = MSG_AT(n);
    if (!sameConv(m.conv, _conv)) continue;
    int len = composeMsg(m, buf, sizeof(buf));
    int nl = wrapLines(buf, len, offs, lens, MAX_WRAP_LINES);
    for (int li = nl - 1; li >= 0 && row >= ROW_MSG_FIRST; li--) {
      if (skip > 0) {
        skip--;
        continue;
      }
      char* seg = &buf[offs[li]];
      char saved = seg[lens[li]];
      seg[lens[li]] = 0;
      _mt->moveCursorXY(1, row);
      _mt->clearLine();
      _mt->print(String(seg));
      seg[lens[li]] = saved;
      row--;
    }
  }
  while (row >= ROW_MSG_FIRST) {
    _mt->moveCursorXY(1, row);
    _mt->clearLine();
    row--;
  }
  if (total == 0) printCentered(ROW_MSG_FIRST + MSG_ROWS / 2, "Aucun message pour l'instant");
}

void MinitelUI::renderHint() {
  const char* hint;
  if (_scroll > 0) hint = " Historique - SUITE:récents";
  else if (_details) hint = " SOMMAIRE:menu GUIDE:masquer";
  else hint = " SOMMAIRE:menu GUIDE:détails";

  char line[HINT_COLS * 3];
  StrHelper::strncpy(line, hint, sizeof(line));
  padCols(line, strlen(line), HINT_COLS, sizeof(line));

  _mt->moveCursorXY(1, ROW_HINT);
  _mt->attributs(INVERSION_FOND);
  _mt->print(String(line));
  _mt->attributs(FOND_NORMAL);
  renderCounter();
}

// how many more characters can still be typed: the stricter of the screen-space
// limit (INPUT_MAX_CHARS) and the on-air size limit (inputMaxBytes, where an
// accent costs 2 bytes but only 1 character)
void MinitelUI::renderCounter() {
  int remaining = INPUT_MAX_CHARS - _input_chars;
  int remaining_bytes = inputMaxBytes() - _input_len;
  if (remaining_bytes < remaining) remaining = remaining_bytes;
  if (remaining < 0) remaining = 0;

  char counter[16];
  snprintf(counter, sizeof(counter), "Reste %3d ", remaining);
  _mt->moveCursorXY(HINT_COLS + 1, ROW_HINT);
  _mt->attributs(INVERSION_FOND);
  _mt->print(String(counter));
  _mt->attributs(FOND_NORMAL);
}

void MinitelUI::renderInput() {
  for (int r = 0; r < INPUT_ROWS; r++) {
    _mt->moveCursorXY(1, ROW_INPUT + r);
    _mt->clearLine();
  }
  _mt->moveCursorXY(1, ROW_INPUT);
  _mt->print("> ");
  _mt->print(String(_input));
}

void MinitelUI::placeCursor() {
  int pos = PROMPT_COLS + _input_chars;
  _mt->moveCursorXY(1 + pos % COLS, ROW_INPUT + pos / COLS);
}

/* ------------------------------------------------------------------------------------ */

// Conversations: channels, then contacts with messages (most recent first), then the other contacts.
// Fills dest with the entries skip .. skip+max_entries-1, returns the total number of entries.
int MinitelUI::buildMenu(Conv* dest, int max_entries, int skip) {
  int k = 0;
  auto add = [&](const Conv& c) {
    if (k >= skip && k < skip + max_entries) dest[k - skip] = c;
    k++;
  };

  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (_mesh.getChannel(i, ch) && ch.name[0]) {
      Conv c = { CONV_CHANNEL, { (uint8_t)i, 0, 0, 0 } };
      add(c);
    }
  }
  for (int n = _msg_count - 1; n >= 0; n--) {
    Msg& m = MSG_AT(n);
    if (m.conv.type != CONV_CONTACT || convContact(m.conv) == NULL) continue;
    bool seen = false;
    for (int j = _msg_count - 1; j > n && !seen; j--) seen = sameConv(MSG_AT(j).conv, m.conv);
    if (!seen) add(m.conv);
  }
  ContactsIterator it = _mesh.startContactsIterator();
  ContactInfo c;
  while (it.hasNext(&_mesh, c)) {
    if (c.type != ADV_TYPE_CHAT && c.type != ADV_TYPE_ROOM) continue;
    Conv conv = { CONV_CONTACT };
    memcpy(conv.id, c.id.pub_key, 4);
    bool has_msgs = false;
    for (int n = 0; n < _msg_count && !has_msgs; n++) has_msgs = sameConv(MSG_AT(n).conv, conv);
    if (!has_msgs) add(conv);
  }
  return k;
}

void MinitelUI::renderMenu() {
  Conv entries[MENU_ROWS];
  int total = buildMenu(entries, MENU_ROWS, _menu_page * MENU_ROWS);
  _menu_pages = total > 0 ? (total + MENU_ROWS - 1) / MENU_ROWS : 1;
  if (_menu_page >= _menu_pages) {   // entries removed meanwhile
    _menu_page = _menu_pages - 1;
    buildMenu(entries, MENU_ROWS, _menu_page * MENU_ROWS);
  }

  _mt->newScreen();
  _mt->cursor();

  char right[16];
  snprintf(right, sizeof(right), "%.2fV ", _board.getBattMilliVolts() / 1000.0f);
  char line[COLS * 3 + 4];
  line[0] = ' ';
  strcpy(&line[1], "SOMMAIRE - Conversations");
  int len = truncateCols(line, COLS - strlen(right));
  len = padCols(line, len, COLS - strlen(right), sizeof(line));
  strcpy(&line[len], right);
  _mt->moveCursorXY(1, ROW_HEADER);
  _mt->attributs(INVERSION_FOND);
  _mt->print(String(line));
  _mt->attributs(FOND_NORMAL);

  printAt(1, MENU_ROW_TOOLS, " 900 Autres fonctions");

  int first = _menu_page * MENU_ROWS;
  int shown = total - first < MENU_ROWS ? total - first : MENU_ROWS;
  for (int r = 0; r < shown; r++) {
    char raw[64];
    convName(entries[r], raw, sizeof(raw));
    char name[COLS * 3];
    sanitize(name, sizeof(name), raw, strlen(raw));
    truncateCols(name, 28);

    int unread = countUnread(&entries[r]);
    char count[12] = "";
    if (unread > 0) snprintf(count, sizeof(count), "(%d)", unread);

    snprintf(line, sizeof(line), "%3d %s", first + r + 1, name);
    len = padCols(line, strlen(line), COLS - 1 - strlen(count), sizeof(line));
    strcpy(&line[len], count);
    printAt(1, MENU_ROW_FIRST + r, line, sameConv(entries[r], _conv));   // current one highlighted
  }

  char pageinfo[16];
  snprintf(pageinfo, sizeof(pageinfo), "page %d/%d ", _menu_page + 1, _menu_pages);
  char hint[COLS * 3 + 4];
  hint[0] = ' ';
  strcpy(&hint[1], "Numéro puis ENVOI : ouvrir");
  len = truncateCols(hint, COLS - strlen(pageinfo));
  len = padCols(hint, len, COLS - strlen(pageinfo), sizeof(hint));
  strcpy(&hint[len], pageinfo);
  printAt(1, 21, hint, true);
  printAt(1, 22, " SUITE/RETOUR:pages  SOMMAIRE:sortir");
  printAt(1, 24, "Numéro : ");
  _mt->print(String(_menu_num_len > 0 ? _menu_num : ""));
}

// "Autres fonctions" : annonces réseau + bascule OLED, hors de la liste des conversations.
void MinitelUI::renderTools() {
  _mt->newScreen();
  _mt->cursor();

  printAt(1, ROW_HEADER, " SOMMAIRE - Autres fonctions            ", true);

  printAt(1, TOOLS_ROW_ZEROHOP, "  1 Annonce : voisins (sans relais)");
  printAt(1, TOOLS_ROW_FLOOD,   "  2 Annonce : tout le réseau (relayée)");
  if (_display) {
    printAt(1, TOOLS_ROW_OLED, _oled_always_on
      ? "  3 Écran OLED : toujours allumé"
      : "  3 Écran OLED : normal (économie)");
  }
  if (_menu_status[0]) printAt(1, TOOLS_ROW_STATUS, _menu_status);

  printAt(1, 21, " Numéro puis ENVOI : choisir            ", true);
  printAt(1, 22, " SOMMAIRE/ANNULATION : retour");
  printAt(1, 24, "Numéro : ");
  _mt->print(String(_menu_num_len > 0 ? _menu_num : ""));
}

#endif
