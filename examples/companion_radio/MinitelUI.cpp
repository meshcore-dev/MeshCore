#ifdef MINITEL_RX

#include "MinitelUI.h"
#include "MyMesh.h"
#include <time.h>
#include <Preferences.h>
#include <Minitel1B_Hard.h>   // keep last: defines many short macros (ESC, CR, SEP, ...)

// Screen layout (rows 1..24, 40 or 80 columns -- see _cols)
#define MAX_COLS       80   // widest supported mode (Mixte); buffers are sized for this
#define ROW_HEADER     1
#define ROW_MSG_FIRST  2
#define MSG_ROWS       19
#define ROW_HINT       21
#define ROW_INPUT      22
#define INPUT_ROWS     3
#define PROMPT_COLS    2
#define COUNTER_COLS   10   // width of the "Reste NNN " counter at the end of the hint bar
#define MENU_ROW_FIRST   3
#define MENU_ROWS        17
#define MENU_ROW_TOOLS   20   // just below the conversation list: entry point to "Autres fonctions"

// reserved menu number for "Autres fonctions": well above any realistic
// conversation count, so it can never collide with a real entry
#define MENU_ACTION_TOOLS  900

// "Autres fonctions": info dashboard on top, settings/actions below
#define TOOLS_ROW_INFO_TITLE      3
#define TOOLS_ROW_BATT            4
#define TOOLS_ROW_FREQ            5
#define TOOLS_ROW_RADIO           6
#define TOOLS_ROW_TXPOWER         7
#define TOOLS_ROW_HEADER_SIZE     8   // path hash size (path_hash_mode + 1), in bytes
#define TOOLS_ROW_SIGNAL          9   // last packet heard, from anyone: RSSI/SNR
#define TOOLS_ROW_BLE             10
#define TOOLS_ROW_BLE_PIN         11  // only shown while pairing is still needed
#define TOOLS_ROW_SEPARATOR       13
#define TOOLS_ROW_SETTINGS_TITLE  14
#define TOOLS_ROW_ZEROHOP  16
#define TOOLS_ROW_FLOOD    17
#define TOOLS_ROW_OLED     18
#define TOOLS_ROW_WIDTH    19
#define TOOLS_ROW_STATUS   20

#define MAX_WRAP_LINES 10

#define PROBE_RETRY_MILLIS    5000
#define PROBE_REPLY_MILLIS    1000
#define PING_INTERVAL_MILLIS  10000
#define PING_REPLY_MILLIS     2000
#define TOOLS_REFRESH_MILLIS  30000  // how often the "Autres fonctions" dashboard (battery, etc.) is refreshed
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
// Switch between Vidéotex (40 columns) and Mixte (80 columns, no semi-graphics) standards.
// Sent raw, no wait for the acknowledgement: the library's own modeMixte()/modeVideotex()
// block indefinitely for a reply, same issue as the other PRO commands above.
static const uint8_t CMD_MODE_MIXTE[]    = { MT_ESC, MT_PRO2, 0x32, 0x7D };  // 40 -> 80 cols
static const uint8_t CMD_MODE_VIDEOTEX[] = { MT_ESC, MT_PRO2, 0x32, 0x7E };  // 80 -> 40 cols
// Mixte comes up in scrolling ("rouleau") mode: any line overflow scrolls the whole
// screen. Switch to page mode so absolute positioning stays put.
static const uint8_t CMD_STOP_ROULEAU[]  = { MT_ESC, MT_PRO2, 0x6A, 0x43 };
// Mixte rows 01-24 follow ISO 6429: SGR for inverse video, ED + CUP to clear.
static const uint8_t CSI_INVERSE_ON[]    = { MT_ESC, MT_CSI, '7', 'm' };
static const uint8_t CSI_INVERSE_OFF[]   = { MT_ESC, MT_CSI, '2', '7', 'm' };
static const uint8_t CSI_CLEAR_HOME[]    = { MT_ESC, MT_CSI, '2', 'J', MT_ESC, MT_CSI, '1', ';', '1', 'H' };
#define MT_SO  0x0E   // Mixte: switch to the French character set
#define MT_SI  0x0F   // Mixte: back to the American set
#define SWITCH_SETTLE_MILLIS  600   // let the Minitel finish a 40/80 switch before drawing
// Replies to our commands (sent when going online or switching 40/80) can arrive garbled,
// and a reply byte that lost its ESC looks like a typed letter: ignore keys meanwhile.
#define KEY_GRACE_MILLIS      1500

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

// Word-wraps sanitized text into lines of at most 'width' columns
static int wrapLines(const char* s, int len, int* offs, int* lens, int max_lines, int width) {
  int n = 0;
  int start = 0;
  do {
    int i = start, cols = 0, last_space = -1;
    while (i < len && cols < width) {
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

  Preferences nvs;
  if (nvs.begin("minitel", true)) {
    _wide = nvs.getBool("wide", false);
    nvs.end();
  }

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
  // Always re-send the mode, even if we think it's already set: the Minitel keeps its
  // mode when only the board restarts, so its actual state is unknown here.
  setCols(_wide ? 80 : 40);
  _mode = MODE_SPLASH;
  _splash_until = millis() + SPLASH_MILLIS;
  _full_dirty = true;
}

// Sent raw, no wait for the acknowledgement; drawing resumes after SWITCH_SETTLE_MILLIS
void MinitelUI::setCols(int cols) {
  _cols = cols;
  if (cols == 80) writeRaw(CMD_MODE_MIXTE, sizeof(CMD_MODE_MIXTE));
  else writeRaw(CMD_MODE_VIDEOTEX, sizeof(CMD_MODE_VIDEOTEX));
  _switch_settle = millis() + SWITCH_SETTLE_MILLIS;
  _keys_ignored_until = millis() + KEY_GRACE_MILLIS;
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
  if (_mode == MODE_TOOLS && !_full_dirty && reached(_next_tools_refresh)) {
    _full_dirty = true;   // refresh the dashboard (battery, etc.) in "Autres fonctions"
    _next_tools_refresh = millis() + TOOLS_REFRESH_MILLIS;
  }

  // apply a new 40/80 choice made in "Autres fonctions"
  if (!_switch_settle && (_cols == 80) != _wide) {
    setCols(_wide ? 80 : 40);
  }

  // a 40/80 switch was just sent: let the Minitel finish it before drawing anything
  if (_switch_settle) {
    if (!reached(_switch_settle)) return;
    _switch_settle = 0;
    if (_cols == 80) writeRaw(CMD_STOP_ROULEAU, sizeof(CMD_STOP_ROULEAU));
    _full_dirty = true;
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
  if (_num_events > 0 && !reached(_keys_ignored_until)) {
    MT_DEBUG("[MT] ignored %d key(s) during grace period\n", _num_events);
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
      // only function keys (ENVOI..CONNEXION/FIN); the Minitel also sends SEP-prefixed
      // acknowledgements (e.g. SEP 0x70/0x71 after a 40/80 switch) that must not act as keys
      if (b >= 0x41 && b <= 0x49) addEvent(b, NULL);
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
  MT_DEBUG("[MT] key 0x%02X '%s'\n", ev.key, ev.text);
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
    case MODE_CREDITS:   // any key goes back to "Autres fonctions"
      _mode = MODE_TOOLS;
      _next_tools_refresh = millis() + TOOLS_REFRESH_MILLIS;
      _full_dirty = true;
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
        text(ev.text);
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
        _next_tools_refresh = millis() + TOOLS_REFRESH_MILLIS;
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
        text(ev.text);
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
      } else if (num == 4) {
        _wide = !_wide;   // applied by loop(); remembered across reboots
        Preferences nvs;
        if (nvs.begin("minitel", false)) {
          nvs.putBool("wide", _wide);
          nvs.end();
        }
        strcpy(_menu_status, _wide
          ? "Affichage : 80 colonnes."
          : "Affichage : 40 colonnes.");
      } else if (num == 5) {
        _mode = MODE_CREDITS;
        _menu_status[0] = 0;
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

// screen-space limit on the 3-row input area: never write the very last cell
int MinitelUI::inputMaxChars() const {
  return INPUT_ROWS * _cols - PROMPT_COLS - 1;
}

void MinitelUI::typeText(const char* utf8) {
  int len = strlen(utf8);
  if (_input_chars >= inputMaxChars() || _input_len + len > inputMaxBytes()) {
    _mt->bip();
    return;
  }
  int pos = PROMPT_COLS + _input_chars;
  memcpy(&_input[_input_len], utf8, len);
  _input_len += len;
  _input[_input_len] = 0;
  _input_chars++;

  _mt->moveCursorXY(1 + pos % _cols, ROW_INPUT + pos / _cols);
  text(utf8);
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
  _mt->moveCursorXY(1 + pos % _cols, ROW_INPUT + pos / _cols);
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
    total += wrapLines(buf, len, offs, lens, MAX_WRAP_LINES, _cols);
  }
  return total;
}

// Base letter for a Latin-1 accented character (second byte of a 0xC3 UTF-8 pair), or 0
static char accentBase(uint8_t b) {
  if (b >= 0x80 && b <= 0x86) return 'A';
  if (b == 0x87) return 'C';
  if (b >= 0x88 && b <= 0x8B) return 'E';
  if (b >= 0x8C && b <= 0x8F) return 'I';
  if (b == 0x91) return 'N';
  if (b >= 0x92 && b <= 0x96) return 'O';
  if (b >= 0x99 && b <= 0x9C) return 'U';
  if (b >= 0xA0 && b <= 0xA6) return 'a';
  if (b == 0xA7) return 'c';
  if (b >= 0xA8 && b <= 0xAB) return 'e';
  if (b >= 0xAC && b <= 0xAF) return 'i';
  if (b == 0xB1) return 'n';
  if (b >= 0xB2 && b <= 0xB6) return 'o';
  if (b >= 0xB9 && b <= 0xBC) return 'u';
  if (b == 0xBD || b == 0xBF) return 'y';
  return 0;
}

// Mixte's French set (reached with SO) only has these five accented letters;
// every other accent is shown as its base letter.
static uint8_t mixteFrench(uint8_t b) {
  switch (b) {
    case 0xA0: return 0x40;   // à
    case 0xA7: return 0x5C;   // ç
    case 0xA9: return 0x7B;   // é
    case 0xB9: return 0x7C;   // ù
    case 0xA8: return 0x7D;   // è
  }
  return 0;
}

void MinitelUI::text(const char* utf8) {
  if (_cols != 80) {
    _mt->print(String(utf8));
    return;
  }
  for (const uint8_t* p = (const uint8_t*)utf8; *p; ) {
    if (*p < 0x80) {
      _mt->writeByte(*p++);
    } else if (*p == 0xC3 && p[1]) {
      uint8_t fr = mixteFrench(p[1]);
      if (fr) {
        _mt->writeByte(MT_SO);
        _mt->writeByte(fr);
        _mt->writeByte(MT_SI);
      } else {
        char base = accentBase(p[1]);
        _mt->writeByte(base ? base : '?');
      }
      p += 2;
    } else {
      // any other multi-byte character: skip its continuation bytes one at a time, so a
      // truncated or malformed sequence can never step over the terminating NUL
      _mt->writeByte('?');
      p++;
      while ((*p & 0xC0) == 0x80) p++;
    }
  }
}

void MinitelUI::inverse(bool on) {
  if (_cols == 80) {
    if (on) writeRaw(CSI_INVERSE_ON, sizeof(CSI_INVERSE_ON));
    else writeRaw(CSI_INVERSE_OFF, sizeof(CSI_INVERSE_OFF));
  } else {
    _mt->attributs(on ? INVERSION_FOND : FOND_NORMAL);
  }
}

void MinitelUI::clearScreen() {
  if (_cols == 80) writeRaw(CSI_CLEAR_HOME, sizeof(CSI_CLEAR_HOME));
  else _mt->newScreen();
}

// attributes are set after positioning the cursor
void MinitelUI::printAt(int col, int row, const char* str, bool inv) {
  _mt->moveCursorXY(col, row);
  if (inv) inverse(true);
  text(str);
  if (inv) inverse(false);
}

void MinitelUI::printCentered(int row, const char* text, bool inverse) {
  char line[MAX_COLS * 3];
  sanitize(line, sizeof(line), text, strlen(text));
  int len = truncateCols(line, _cols);
  int col = 1 + (_cols - countChars(line, len)) / 2;
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
      clearScreen();
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
    case MODE_CREDITS:
      renderSplash(true);
      break;
  }
  _full_dirty = _msgs_dirty = _header_dirty = false;
}

// 4-line ASCII-art banners (figlet "small" style) for Mixte, which has no double size.
// One string per letter on each line, so the columns line up in the source.
static const char* const ART_MESHCORE[] = {
  " __  __ "  "     " "    " " _    " "  ___ " "     " "     " "     ",
  "|  \\/  |" " ___ " " ___" "| |_  " " / __|" " ___ " " _ _ " " ___ ",
  "| |\\/| |" "/ -_)" "(_-<" "| ' \\ " "| (__ " "/ _ \\" "| '_|" "/ -_)",
  "|_|  |_|" "\\___|" "/__/" "|_||_|" " \\___|" "\\___/" "|_|  " "\\___|",
};
// The pairing screen's banner starts with the Bluetooth rune, so it's not mistaken for the splash
static const char* const ART_BLUETOOTH[] = {
  "  |\\    "
  " ___ " " _ " "      " "     " " _   " "     " "     " " _   " " _    ",
  "\\ | )   "
  "| _ )" "| |" " _  _ " " ___ " "| |_ " " ___ " " ___ " "| |_ " "| |_  ",
  "/ | )   "
  "| _ \\" "| |" "| || |" "/ -_)" "|  _|" "/ _ \\" "/ _ \\" "|  _|" "| ' \\ ",
  "  |/    "
  "|___/" "|_|" " \\_,_|" "\\___|" " \\__|" "\\___/" "\\___/" " \\__|" "|_||_|",
};

// Mixte only: ISO 6429 attribute (SGR), e.g. "1" bold, "4" underline, "5" blink, "22"/"24"/"25" off
void MinitelUI::sgr(const char* params) {
  if (_cols != 80) return;
  _mt->writeByte(MT_ESC);
  _mt->writeByte(MT_CSI);
  for (const char* p = params; *p; p++) _mt->writeByte(*p);
  _mt->writeByte('m');
}

// Screen title: double size in Vidéotex; a bold ASCII-art banner (rows row-1 .. row+2) in Mixte
void MinitelUI::renderTitle(int col40, int row, const char* title, const char* const* art) {
  if (_cols == 80) {
    int col = 1 + (_cols - (int)strlen(art[0])) / 2;
    sgr("1");
    for (int i = 0; i < 4; i++) printAt(col, row - 1 + i, art[i]);
    sgr("22");
    return;
  }
  _mt->moveCursorXY(col40, row);
  _mt->attributs(DOUBLE_GRANDEUR);
  _mt->print(title);
  _mt->attributs(GRANDEUR_NORMALE);
}

// Splash screen at start-up; also the "Crédits" page of "Autres fonctions" (different footer)
void MinitelUI::renderSplash(bool credits) {
  clearScreen();
  if (_cols == 40) _mt->noCursor();   // the cursor can't be turned off in Mixte
  renderTitle(13, 3, "MeshCore", ART_MESHCORE);   // 8 letters, double width: 16 columns
  sgr("4");
  printCentered(6, "Messagerie LoRa sur Minitel");
  sgr("24");

  printCentered(9, "Une réalisation de");
  printCentered(10, " Capt_Pierrot & Jonathan ", true);
  printCentered(11, "github.com/Jonathan2050/minitelmesh");

  printCentered(13, "Basé sur :");
  printCentered(14, "MeshCore - github.com/meshcore-dev");
  printCentered(16, "Minitel1B_Hard - Eric Sérandour");
  printCentered(17, "github.com/eserandour/Minitel1B_Hard");

  char line[80];
  snprintf(line, sizeof(line), "MinitelMesh %s - MeshCore %s", MINITEL_VERSION, FIRMWARE_VERSION);
  printCentered(20, line);
  printCentered(21, _mesh.getNodeName());
  if (credits) {
    const int footer_cols = (_cols == 80) ? 17 : 15;   // "SOMMAIRE" button (+ margins in 80) + " retour"
    snprintf(line, sizeof(line), "%*s[SOMMAIRE] retour", (_cols - footer_cols) / 2, "");
    printKeys(23, line);
    return;
  }
  sgr("5");
  printCentered(23, "Appuyez sur une touche");
  sgr("25");
}

// Shown after the splash screen while no phone has connected over Bluetooth yet
void MinitelUI::renderPairing() {
  clearScreen();
  if (_cols == 40) _mt->noCursor();
  renderTitle(10, 3, "Bluetooth", ART_BLUETOOTH);
  sgr("4");
  printCentered(6, "En attente de l'application MeshCore");
  sgr("24");

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
  sgr("5");
  printCentered(23, "Appuyez sur une touche pour l'ignorer");
  sgr("25");
}

void MinitelUI::renderHeader() {
  char raw[64];
  convName(_conv, raw, sizeof(raw));
  char name[MAX_COLS * 3];
  sanitize(name, sizeof(name), raw, strlen(raw));

  char right[32];
  int unread = countUnread(NULL);
  right[0] = 0;
  if (unread > 0) snprintf(right, sizeof(right), "%d non lu%s ", unread, unread > 1 ? "s" : "");

  char line[MAX_COLS * 3 + 4];
  line[0] = ' ';
  strcpy(&line[1], name);
  int len = truncateCols(line, _cols - strlen(right) - 1);
  len = padCols(line, len, _cols - strlen(right), sizeof(line));
  strcpy(&line[len], right);

  _mt->moveCursorXY(1, ROW_HEADER);
  inverse(true);
  text(line);
  inverse(false);
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
    int nl = wrapLines(buf, len, offs, lens, MAX_WRAP_LINES, _cols);
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
      text(seg);
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

// A help line where "[KEY]" marks a function key, drawn as an inverse-video "button"
// (like the services of the time), the explanation around it in normal video.
void MinitelUI::printKeys(int row, const char* markup) {
  _mt->moveCursorXY(1, row);
  _mt->clearLine();
  char seg[MAX_COLS * 3];   // markup is a single screen line: always fits
  bool pad = (_cols == 80);   // in 40 columns the help lines have no room for button margins
  int n = 0;
  for (const char* p = markup; ; p++) {
    if (*p == '[' || *p == ']' || *p == 0) {
      if (*p == ']' && pad) seg[n++] = ' ';   // button margin on the right
      seg[n] = 0;
      text(seg);
      n = 0;
      if (*p == 0) break;
      if (*p == '[') { inverse(true); if (pad) seg[n++] = ' '; }   // ...and on the left
      if (*p == ']') inverse(false);
      continue;
    }
    seg[n++] = *p;
  }
}

void MinitelUI::renderHint() {
  if (_scroll > 0) printKeys(ROW_HINT, " Historique  [SUITE] récents");
  else if (_details) printKeys(ROW_HINT, " [SOMMAIRE] menu  [GUIDE] masquer");
  else printKeys(ROW_HINT, " [SOMMAIRE] menu  [GUIDE] détails");
  renderCounter();
}

// how many more characters can still be typed: the stricter of the screen-space
// limit (inputMaxChars) and the on-air size limit (inputMaxBytes, where an
// accent costs 2 bytes but only 1 character)
void MinitelUI::renderCounter() {
  int remaining = inputMaxChars() - _input_chars;
  int remaining_bytes = inputMaxBytes() - _input_len;
  if (remaining_bytes < remaining) remaining = remaining_bytes;
  if (remaining < 0) remaining = 0;

  char counter[16];
  snprintf(counter, sizeof(counter), "Reste %3d ", remaining);
  _mt->moveCursorXY(_cols - COUNTER_COLS + 1, ROW_HINT);
  text(counter);
}

void MinitelUI::renderInput() {
  for (int r = 0; r < INPUT_ROWS; r++) {
    _mt->moveCursorXY(1, ROW_INPUT + r);
    _mt->clearLine();
  }
  _mt->moveCursorXY(1, ROW_INPUT);
  text("> ");
  text(_input);
}

void MinitelUI::placeCursor() {
  int pos = PROMPT_COLS + _input_chars;
  _mt->moveCursorXY(1 + pos % _cols, ROW_INPUT + pos / _cols);
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

  clearScreen();
  _mt->cursor();

  char line[MAX_COLS * 3 + 4];
  line[0] = ' ';
  strcpy(&line[1], "SOMMAIRE - Conversations");
  int len = truncateCols(line, _cols);
  len = padCols(line, len, _cols, sizeof(line));
  _mt->moveCursorXY(1, ROW_HEADER);
  inverse(true);
  text(line);
  inverse(false);

  printAt(1, MENU_ROW_TOOLS, " 900 Autres fonctions");

  int first = _menu_page * MENU_ROWS;
  int shown = total - first < MENU_ROWS ? total - first : MENU_ROWS;
  for (int r = 0; r < shown; r++) {
    char raw[64];
    convName(entries[r], raw, sizeof(raw));
    char name[MAX_COLS * 3];
    sanitize(name, sizeof(name), raw, strlen(raw));
    truncateCols(name, _cols - 12);

    int unread = countUnread(&entries[r]);
    char count[12] = "";
    if (unread > 0) snprintf(count, sizeof(count), "(%d)", unread);

    snprintf(line, sizeof(line), "%3d %s", first + r + 1, name);
    len = padCols(line, strlen(line), _cols - 1 - strlen(count), sizeof(line));
    strcpy(&line[len], count);
    printAt(1, MENU_ROW_FIRST + r, line, sameConv(entries[r], _conv));   // current one highlighted
  }

  printKeys(21, " Numéro puis [ENVOI] ouvrir");
  char pageinfo[16];
  snprintf(pageinfo, sizeof(pageinfo), "page %d/%d ", _menu_page + 1, _menu_pages);
  printAt(_cols - strlen(pageinfo) + 1, 21, pageinfo);
  printKeys(22, " [SUITE] [RETOUR] pages  [SOMMAIRE] sortir");
  printAt(1, 24, "Numéro : ");
  text(_menu_num_len > 0 ? _menu_num : "");
}

// "Autres fonctions" : tableau de bord (batterie, radio, Bluetooth) en haut,
// annonces réseau + bascule OLED en bas, hors de la liste des conversations.
void MinitelUI::renderTools() {
  clearScreen();
  _mt->cursor();

  char line[MAX_COLS * 3 + 4];

  strcpy(line, " SOMMAIRE - Autres fonctions");
  padCols(line, strlen(line), _cols, sizeof(line));
  printAt(1, ROW_HEADER, line, true);

  strcpy(line, " Informations");
  padCols(line, strlen(line), _cols, sizeof(line));
  printAt(1, TOOLS_ROW_INFO_TITLE, line, true);

  NodePrefs* prefs = _mesh.getNodePrefs();

  snprintf(line, sizeof(line), "  Batterie       %.2fV", _board.getBattMilliVolts() / 1000.0f);
  printAt(1, TOOLS_ROW_BATT, line);

  snprintf(line, sizeof(line), "  Fréquence      %.3f MHz", prefs->freq);
  printAt(1, TOOLS_ROW_FREQ, line);

  snprintf(line, sizeof(line), "  Radio          SF%d  BW%.0fkHz  CR%d", prefs->sf, prefs->bw, prefs->cr);
  printAt(1, TOOLS_ROW_RADIO, line);

  snprintf(line, sizeof(line), "  Puissance TX   %d dBm", prefs->tx_power_dbm);
  printAt(1, TOOLS_ROW_TXPOWER, line);

  snprintf(line, sizeof(line), "  Taille entête  %d octet%s", prefs->path_hash_mode + 1, prefs->path_hash_mode + 1 > 1 ? "s" : "");
  printAt(1, TOOLS_ROW_HEADER_SIZE, line);

  snprintf(line, sizeof(line), "  Signal reçu    %d dBm  SNR %ddB", (int)_mesh.getLastRSSI(), (int)_mesh.getLastSNR());
  printAt(1, TOOLS_ROW_SIGNAL, line);

  snprintf(line, sizeof(line), "  Téléphone BLE  %s", _mesh.isBLEConnected() ? "connecté" : "non connecté");
  printAt(1, TOOLS_ROW_BLE, line);

  if (needsPairing()) {
    snprintf(line, sizeof(line), "  Code Bluetooth %06u", (unsigned)_mesh.getBLEPin());
    printAt(1, TOOLS_ROW_BLE_PIN, line);
  }

  _mt->hLine(1, TOOLS_ROW_SEPARATOR, _cols, CENTER);

  strcpy(line, " Réglages");
  padCols(line, strlen(line), _cols, sizeof(line));
  printAt(1, TOOLS_ROW_SETTINGS_TITLE, line, true);

  printAt(1, TOOLS_ROW_ZEROHOP, "  1 Annonce : voisins (sans relais)");
  printAt(1, TOOLS_ROW_FLOOD,   "  2 Annonce : tout le réseau (relayée)");
  if (_display) {
    printAt(1, TOOLS_ROW_OLED, _oled_always_on
      ? "  3 Écran OLED : toujours allumé"
      : "  3 Écran OLED : normal (économie)");
  }
  printAt(1, TOOLS_ROW_WIDTH, _wide
    ? "  4 Affichage : 80 colonnes"
    : "  4 Affichage : 40 colonnes");
  // second column in 80; in 40 it just fits to the right of option 4
  if (_cols == 80) printAt(45, TOOLS_ROW_ZEROHOP, "5 Crédits");
  else printAt(31, TOOLS_ROW_WIDTH, "5 Crédits");
  if (_menu_status[0]) printAt(1, TOOLS_ROW_STATUS, _menu_status);

  printKeys(21, " Numéro puis [ENVOI] choisir");
  printKeys(22, " [SOMMAIRE] ou [ANNULATION] retour");
  printAt(1, 24, "Numéro : ");
  text(_menu_num_len > 0 ? _menu_num : "");
}

#endif
