#ifdef MINITEL_RX

#include "MinitelUI.h"
#include "MyMesh.h"
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

#define PROBE_RETRY_MILLIS   5000
#define PROBE_REPLY_MILLIS   1000
#define PING_INTERVAL_MILLIS 15000
#define PING_REPLY_MILLIS    2000
#define HEADER_REFRESH_MILLIS 60000

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
  _mt = new Minitel(mt_serial, MINITEL_RX, MINITEL_TX);   // starts the port at 1200 bauds 7E1
  _state = PROBE_WAIT;
  _deadline = millis();
}

void MinitelUI::writeRaw(const uint8_t* data, int len) {
  for (int i = 0; i < len; i++) _mt->writeByte(data[i]);
}

// Step 1: maybe the Minitel is already at 4800 (ESP restarted, Minitel kept on)
void MinitelUI::startProbe() {
  mt_serial.updateBaudRate(4800);
  writeRaw(CMD_QUERY_SPEED, sizeof(CMD_QUERY_SPEED));
  _state = PROBE_QUERY;
  _deadline = millis() + PROBE_REPLY_MILLIS;
}

void MinitelUI::goOnline() {
  _state = ONLINE;
  _ping_pending = false;
  _next_ping = millis() + PING_INTERVAL_MILLIS;
  writeRaw(CMD_ECHO_OFF, sizeof(CMD_ECHO_OFF));
  writeRaw(CMD_LOWERCASE, sizeof(CMD_LOWERCASE));
  _full_dirty = true;
}

void MinitelUI::goOffline() {
  mt_serial.discard();   // pending screen data is useless now
  _state = PROBE_WAIT;
  _deadline = millis() + PROBE_RETRY_MILLIS;
}

void MinitelUI::onSpeedReply(uint8_t code) {
  if (code != MT_SPEED_4800) return;
  if (_state == ONLINE) {
    _ping_pending = false;
  } else {
    goOnline();
  }
}

void MinitelUI::loop() {
  if (_mt == NULL) return;

  mt_serial.pump();
  while (mt_serial.available() > 0) {
    handleByte(mt_serial.read() & 0x7F);
  }

  switch (_state) {
    case PROBE_WAIT:
      if (reached(_deadline)) startProbe();
      return;
    case PROBE_QUERY:
      if (reached(_deadline)) {
        // Step 2: Minitel just powered on (1200 bauds): ask it to switch to 4800
        mt_serial.updateBaudRate(1200);
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

  // only draw when the TX queue has room for a whole update
  int free_tx = mt_serial.queueFree();
  if (_full_dirty) {
    if (free_tx > 3000) renderAll();
  } else if (_msgs_dirty) {
    if (free_tx > 2000) {
      renderMessages();
      renderHint();
      placeCursor();
      _msgs_dirty = false;
    }
  } else if (_header_dirty && free_tx > 200) {
    renderHeader();
    placeCursor();
  }
  mt_serial.pump();
}

/* ------------------------------------------------------------------------------------ */

void MinitelUI::handleByte(uint8_t b) {
  switch (_rx) {
    case RX_IDLE:
      if (b == MT_SEP) _rx = RX_SEP;
      else if (b == MT_SS2) _rx = RX_SS2;
      else if (b == MT_ESC) _rx = RX_ESC;
      else if (b >= 0x20 && b < 0x7F && _state == ONLINE) {
        char s[2] = { (char)b, 0 };
        typeText(s);
      }
      break;
    case RX_SEP:
      _rx = RX_IDLE;
      if (_state == ONLINE) onFunctionKey(b);
      break;
    case RX_SS2:
      if (b == 0x41 || b == 0x42 || b == 0x43 || b == 0x48 || b == 0x4B) {
        _rx_accent = b;
        _rx = RX_SS2_ACCENT;
      } else {
        _rx = RX_IDLE;
        const char* s = g2Char(b);
        if (s && _state == ONLINE) typeText(s);
      }
      break;
    case RX_SS2_ACCENT:
      if (b == MT_SS2) {   // accent key pressed again
        _rx = RX_SS2;
      } else {
        _rx = RX_IDLE;
        const char* s = composeAccent(_rx_accent, b);
        if (s && _state == ONLINE) typeText(s);
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

void MinitelUI::onFunctionKey(uint8_t key) {
  switch (key) {
    case KEY_ENVOI:
      sendInput();
      break;
    case KEY_CORRECTION:
      backspace();
      break;
    case KEY_ANNULATION:
      clearInput();
      break;
    case KEY_RETOUR:   // older messages
      _scroll += MSG_ROWS - 1;
      _msgs_dirty = true;
      break;
    case KEY_SUITE:    // newer messages
      _scroll -= MSG_ROWS - 1;
      if (_scroll < 0) _scroll = 0;
      _msgs_dirty = true;
      break;
    case KEY_SOMMAIRE:
      _scroll = 0;
      _msgs_dirty = true;
      break;
    case KEY_REPETITION:
    case KEY_GUIDE:
      _full_dirty = true;
      break;
  }
}

int MinitelUI::inputMaxBytes() {
  int max_bytes = MAX_TEXT_LEN - strlen(_mesh.getNodeName()) - 2;   // sent as "<name>: <text>"
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
}

void MinitelUI::backspace() {
  if (_input_len == 0) return;
  do {
    _input_len--;
  } while (_input_len > 0 && ((uint8_t)_input[_input_len] & 0xC0) == 0x80);
  _input[_input_len] = 0;
  _input_chars--;

  int pos = PROMPT_COLS + _input_chars;
  _mt->moveCursorXY(1 + pos % COLS, ROW_INPUT + pos / COLS);
  _mt->printChar(' ');
  _mt->moveCursorXY(1 + pos % COLS, ROW_INPUT + pos / COLS);
}

void MinitelUI::clearInput() {
  _input_len = _input_chars = 0;
  _input[0] = 0;
  if (_state == ONLINE) {
    renderInput();
    placeCursor();
  }
}

void MinitelUI::sendInput() {
  if (_input_len == 0) return;

  ChannelDetails ch;
  uint32_t timestamp = _mesh.getRTCClock()->getCurrentTimeUnique();
  if (!_mesh.getChannel(MINITEL_CHANNEL_IDX, ch)
      || !_mesh.sendGroupMessage(timestamp, ch.channel, _mesh.getNodeName(), _input, _input_len)) {
    _mt->bip();
    return;
  }
  onChannelMessage(MINITEL_CHANNEL_IDX, _mesh.getNodeName(), _input, _input_len);
  _scroll = 0;
  clearInput();
}

void MinitelUI::onChannelMessage(uint8_t channel_idx, const char* sender, const char* text, int text_len) {
  if (channel_idx != MINITEL_CHANNEL_IDX) return;

  char raw[MINITEL_MSG_SLOT + 40];
  int len = 0;
  if (sender) {
    len = snprintf(raw, sizeof(raw), "%s: ", sender);
  }
  if (text_len > (int)sizeof(raw) - 1 - len) text_len = sizeof(raw) - 1 - len;
  memcpy(&raw[len], text, text_len);
  len += text_len;
  raw[len] = 0;

  sanitize(_msgs[_msg_head], MINITEL_MSG_SLOT, raw, len);
  _msg_head = (_msg_head + 1) % MINITEL_HISTORY;
  if (_msg_count < MINITEL_HISTORY) _msg_count++;
  _msgs_dirty = true;

  if (sender == NULL && _mt && _state == ONLINE) _mt->bip();
}

/* ------------------------------------------------------------------------------------ */

// Word-wraps all stored messages, oldest first
int MinitelUI::buildLines(LineRef* lines, int max_lines) {
  int n = 0;
  for (int m = 0; m < _msg_count; m++) {
    int idx = (_msg_head - _msg_count + m + MINITEL_HISTORY) % MINITEL_HISTORY;
    const char* s = _msgs[idx];
    int len = strlen(s);
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
      if (n >= max_lines) {   // keep the newest lines
        memmove(&lines[0], &lines[1], (max_lines - 1) * sizeof(LineRef));
        n--;
      }
      lines[n].msg = idx;
      lines[n].off = start;
      lines[n].len = end - start;
      n++;
      start = end;
      while (start < len && s[start] == ' ') start++;
    } while (start < len);
  }
  return n;
}

void MinitelUI::renderAll() {
  _mt->newScreen();
  _mt->cursor();
  renderHeader();
  renderMessages();
  renderHint();
  renderInput();
  placeCursor();
  _full_dirty = _msgs_dirty = false;
}

void MinitelUI::renderHeader() {
  char chan_name[32] = "?";
  ChannelDetails ch;
  if (_mesh.getChannel(MINITEL_CHANNEL_IDX, ch)) {
    StrHelper::strncpy(chan_name, ch.name, sizeof(chan_name));
  }
  char raw[80];
  snprintf(raw, sizeof(raw), " #%s - %s", chan_name, _mesh.getNodeName());
  char line[COLS * 3];
  sanitize(line, sizeof(line), raw, strlen(raw));
  int len = truncateCols(line, COLS - 7);

  char batt[8];
  snprintf(batt, sizeof(batt), "%.2fV ", _board.getBattMilliVolts() / 1000.0f);
  int pad = COLS - countChars(line, len) - strlen(batt);
  while (pad-- > 0) line[len++] = ' ';
  strcpy(&line[len], batt);

  _mt->moveCursorXY(1, ROW_HEADER);
  _mt->attributs(INVERSION_FOND);
  _mt->print(String(line));
  _mt->attributs(FOND_NORMAL);
  _header_dirty = false;
}

void MinitelUI::renderMessages() {
  static LineRef lines[MINITEL_HISTORY * 6];
  int n = buildLines(lines, sizeof(lines) / sizeof(lines[0]));

  int max_scroll = n > MSG_ROWS ? n - MSG_ROWS : 0;
  if (_scroll > max_scroll) _scroll = max_scroll;
  int first = n - _scroll - MSG_ROWS;

  char buf[MINITEL_MSG_SLOT];
  for (int r = 0; r < MSG_ROWS; r++) {
    _mt->moveCursorXY(1, ROW_MSG_FIRST + r);
    _mt->clearLine();
    int i = first + r;
    if (i >= 0) {
      memcpy(buf, &_msgs[lines[i].msg][lines[i].off], lines[i].len);
      buf[lines[i].len] = 0;
      _mt->print(String(buf));
    }
  }
}

void MinitelUI::renderHint() {
  _mt->moveCursorXY(1, ROW_HINT);
  _mt->clearLine();
  _mt->attributs(INVERSION_FOND);
  if (_scroll > 0) {
    _mt->print(" Historique  SUITE:recents SOMMAIRE:fin ");
  } else {
    _mt->print(" ENVOI:envoyer  RETOUR:historique       ");
  }
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

#endif
