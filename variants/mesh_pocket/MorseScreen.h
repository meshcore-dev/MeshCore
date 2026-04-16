#pragma once

// =============================================================================
// MorseScreen — single-button Morse compose/receive for the Meshpocket
//
// Entered from the home screen via a triple-click on the USER button when
// MORSE_COMPOSE_ENABLED is defined (Meshpocket companion builds only).
//
// While active, this screen takes exclusive ownership of the USER button:
// - Short press  -> dot (<240 ms by default)
// - Longer press -> dash (>=240 ms)
// - Letter gap (~360 ms silence) commits the staged pattern to the buffer
// - Word gap (~840 ms silence) inserts a space
// - `WW` prosign (.--.--)  -> send to Public (channel 0), clear buffer
// - `HH` prosign (........) -> backspace one character
// - 5 s continuous hold -> exit back to home screen
//
// The screen maintains its own tiny ring buffer of the most recent Public
// channel messages (populated from UITask::newMsg when channel_idx == 0) so
// that it does not need to reach into ChannelScreen internals.
//
// Sending is delegated to UITask via the consumeSendRequest() flag pattern
// so that this header has no dependency on MyMesh / BaseChatMesh types.
// =============================================================================

#ifdef MORSE_COMPOSE_ENABLED

#include <Arduino.h>
#include <string.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/MomentaryButton.h>
#include <MeshCore.h>

// user_btn is instantiated in variants/mesh_pocket/target.cpp
extern MomentaryButton user_btn;

// -----------------------------------------------------------------------------
// Tunables
// -----------------------------------------------------------------------------

// Standard Morse timing: WPM = 1.2 / dot_seconds
// 10 WPM -> dot = 120 ms
#define MORSE_DOT_UNIT_MS      120

// Press shorter than this = dot, longer = dash.
// 2x dot is a common midpoint threshold (dash is nominally 3x dot).
#define MORSE_DOT_DASH_MS      (MORSE_DOT_UNIT_MS * 2)

// Inter-letter silence that commits the staged pattern (3 dot units).
#define MORSE_LETTER_GAP_MS    (MORSE_DOT_UNIT_MS * 3)

// Inter-word silence that inserts a space (7 dot units).
#define MORSE_WORD_GAP_MS      (MORSE_DOT_UNIT_MS * 7)

// Exit gesture — longer than any conceivable dash, dominant hand will tire.
#define MORSE_EXIT_HOLD_MS     5000

// Buffer sizes
#define MORSE_OUT_BUF_LEN      134   // MeshCore per-channel msg cap is ~133
#define MORSE_STAGING_MAX      12    // longest pattern we accept (HH = 8)
#define MORSE_INBOX_SIZE       3
#define MORSE_INBOX_TEXT_LEN   96
#define MORSE_INBOX_NAME_LEN   32

// -----------------------------------------------------------------------------
// Morse lookup — ITU minimal + basic punctuation
// Stored in flash; tiny (~400 bytes). RAM impact: zero.
// -----------------------------------------------------------------------------
struct MorseEntry {
  char        c;
  const char* pat;
};

static const MorseEntry MORSE_TABLE[] = {
  {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
  {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
  {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
  {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
  {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
  {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
  {'Y', "-.--"},  {'Z', "--.."},
  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
  {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
  {'8', "---.."}, {'9', "----."},
  {'.', ".-.-.-"},{',', "--..--"},{'?', "..--.."},
  {0, nullptr}
};

// -----------------------------------------------------------------------------
class MorseScreen : public UIScreen {
  mesh::RTCClock* _rtc;

  // Outgoing composition
  char      _outBuf[MORSE_OUT_BUF_LEN];
  uint16_t  _outLen;

  // Current letter staging (dots/dashes not yet decoded)
  char      _staging[MORSE_STAGING_MAX];
  uint8_t   _stagingLen;

  // Key timing state
  bool          _btnPrevPressed;
  unsigned long _pressStart;
  unsigned long _releaseAt;       // 0 if not yet released after last press
  bool          _letterDecoded;   // set after commitStaging() — awaits word gap
  bool          _wordSpaceInserted;
  bool          _exitArmed;       // hold threshold crossed; exits on release

  // Cross-screen requests (UITask polls these)
  bool          _wantsExit;
  bool          _wantsSend;

  // Incoming ring buffer — channel 0 (Public) only
  struct InboxEntry {
    uint32_t timestamp;
    char     from[MORSE_INBOX_NAME_LEN];
    char     text[MORSE_INBOX_TEXT_LEN];
    bool     valid;
  };
  InboxEntry _inbox[MORSE_INBOX_SIZE];
  uint8_t    _inboxNewest;     // index of most recent entry
  uint8_t    _inboxCount;

  bool          _dirty;
  unsigned long _nextRender;

  // ---------------------------------------------------------------------------
  // Morse decode
  // Returns the ASCII character for a pattern, or:
  //   '\x01'  = WW prosign  ".--.--"  (send) — W·W without letter gap
  //   '\x02'  = HH prosign  "........" (backspace)
  //   0       = no match (silently drop)
  // ---------------------------------------------------------------------------
  char decodeStaging() const {
    if (_stagingLen == 0) return 0;
    if (strcmp(_staging, ".--.--") == 0)   return '\x01';
    if (strcmp(_staging, "........") == 0) return '\x02';
    for (const MorseEntry* e = MORSE_TABLE; e->c != 0; e++) {
      if (strcmp(_staging, e->pat) == 0) return e->c;
    }
    return 0;
  }

  void commitStaging() {
    if (_stagingLen == 0) return;
    char decoded = decodeStaging();
    if (decoded == '\x01') {
      // WW — request send from UITask
      if (_outLen > 0) _wantsSend = true;
    } else if (decoded == '\x02') {
      // HH — backspace one character (skip trailing space if present)
      if (_outLen > 0) {
        _outLen--;
        _outBuf[_outLen] = 0;
      }
    } else if (decoded != 0) {
      if (_outLen < MORSE_OUT_BUF_LEN - 1) {
        _outBuf[_outLen++] = decoded;
        _outBuf[_outLen] = 0;
      }
    }
    _stagingLen = 0;
    _staging[0] = 0;
    _letterDecoded = true;
    _wordSpaceInserted = false;
    _dirty = true;
  }

  void insertWordSpace() {
    if (_outLen > 0 && _outBuf[_outLen - 1] != ' '
        && _outLen < MORSE_OUT_BUF_LEN - 1) {
      _outBuf[_outLen++] = ' ';
      _outBuf[_outLen] = 0;
      _dirty = true;
    }
    _wordSpaceInserted = true;
  }

public:
  MorseScreen(mesh::RTCClock* rtc)
    : _rtc(rtc),
      _outLen(0), _stagingLen(0),
      _btnPrevPressed(false), _pressStart(0), _releaseAt(0),
      _letterDecoded(false), _wordSpaceInserted(false), _exitArmed(false),
      _wantsExit(false), _wantsSend(false),
      _inboxNewest(0), _inboxCount(0),
      _dirty(true), _nextRender(0)
  {
    _outBuf[0] = 0;
    _staging[0] = 0;
    memset(_inbox, 0, sizeof(_inbox));
  }

  // Called by UITask when the screen is activated (on triple-click from home)
  // Resets composition state so each session starts clean.
  void activate() {
    _outLen = 0;       _outBuf[0] = 0;
    _stagingLen = 0;   _staging[0] = 0;
    _btnPrevPressed = user_btn.isPressed();
    _pressStart = 0;
    _releaseAt = 0;
    _letterDecoded = false;
    _wordSpaceInserted = false;
    _exitArmed = false;
    _wantsExit = false;
    _wantsSend = false;
    _dirty = true;
  }

  // Called from UITask::newMsg for incoming messages.
  // `from` is the sender/channel name; `text` is the message body.
  void notifyPublicMsg(const char* from, const char* text) {
    _inboxNewest = (_inboxCount == 0) ? 0 : ((_inboxNewest + 1) % MORSE_INBOX_SIZE);
    InboxEntry& e = _inbox[_inboxNewest];
    e.timestamp = _rtc ? _rtc->getCurrentTime() : 0;
    if (from) {
      strncpy(e.from, from, MORSE_INBOX_NAME_LEN - 1);
      e.from[MORSE_INBOX_NAME_LEN - 1] = 0;
    } else {
      e.from[0] = 0;
    }
    if (text) {
      strncpy(e.text, text, MORSE_INBOX_TEXT_LEN - 1);
      e.text[MORSE_INBOX_TEXT_LEN - 1] = 0;
    } else {
      e.text[0] = 0;
    }
    e.valid = true;
    if (_inboxCount < MORSE_INBOX_SIZE) _inboxCount++;
    _dirty = true;
  }

  // ---------------------------------------------------------------------------
  // UITask bridges — polled each loop iteration
  // ---------------------------------------------------------------------------

  // Returns the outgoing buffer pointer if a send was requested (WW prosign).
  // Caller clears the buffer via clearOutBuf() after a successful send.
  bool consumeSendRequest(const char** textOut) {
    if (!_wantsSend) return false;
    _wantsSend = false;
    if (textOut) *textOut = _outBuf;
    return true;
  }

  bool wantsExit() const { return _wantsExit; }
  void acknowledgeExit() { _wantsExit = false; }

  void clearOutBuf() {
    _outLen = 0;
    _outBuf[0] = 0;
    _dirty = true;
  }

  // ---------------------------------------------------------------------------
  // UIScreen contract
  // ---------------------------------------------------------------------------

  void poll() override {
    unsigned long now = millis();
    bool pressed = user_btn.isPressed();

    if (pressed && !_btnPrevPressed) {
      // Edge: released -> pressed
      _pressStart = now;
      _exitArmed = false;
      _letterDecoded = false;
      _wordSpaceInserted = false;
    } else if (!pressed && _btnPrevPressed) {
      // Edge: pressed -> released
      unsigned long dur = now - _pressStart;
      if (_exitArmed) {
        // Exit-hold completed — signal UITask to navigate back to home.
        // Do NOT add this press to staging.
        _wantsExit = true;
      } else {
        // Normal dot/dash
        if (_stagingLen < MORSE_STAGING_MAX - 1) {
          _staging[_stagingLen++] = (dur < MORSE_DOT_DASH_MS) ? '.' : '-';
          _staging[_stagingLen] = 0;
        }
        _releaseAt = now;
        _dirty = true;
      }
    } else if (pressed && _btnPrevPressed) {
      // Still holding — check for exit-arm threshold
      if (!_exitArmed && (now - _pressStart) >= MORSE_EXIT_HOLD_MS) {
        _exitArmed = true;
        _dirty = true;   // redraw to show "release to exit" hint
      }
    } else {
      // Idle (not pressed, wasn't pressed) — check gap timers
      if (_stagingLen > 0 && _releaseAt > 0
          && (now - _releaseAt) >= MORSE_LETTER_GAP_MS) {
        commitStaging();
        _releaseAt = now;   // reset so word gap measures from commit
      } else if (_outLen > 0 && _letterDecoded && !_wordSpaceInserted
                 && _releaseAt > 0
                 && (now - _releaseAt) >= MORSE_WORD_GAP_MS) {
        insertWordSpace();
      }
    }

    _btnPrevPressed = pressed;
  }

  int render(DisplayDriver& display) override {
    const int W = display.width();

    display.setTextSize(1);

    // ---- Header --------------------------------------------------------------
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, 0);
    display.print("MORSE");

    display.setColor(_exitArmed ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
    display.drawTextRightAlign(W - 1, 0, _exitArmed ? "Release=exit" : "Hold=exit");

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, W, 1);

    // ---- Inbox (last 2 messages) ---------------------------------------------
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 13);
    display.print("IN");

    display.setColor(DisplayDriver::LIGHT);
    if (_inboxCount == 0) {
      display.setCursor(18, 13);
      display.print("(no messages)");
    } else {
      int y = 13;
      for (int i = 0; i < _inboxCount && i < 2; i++) {
        int idx = (int)_inboxNewest - i;
        while (idx < 0) idx += MORSE_INBOX_SIZE;
        const InboxEntry& e = _inbox[idx];
        if (!e.valid) continue;
        display.drawTextEllipsized(18, y, W - 20, e.text);
        y += 10;
      }
    }

    display.drawRect(0, 33, W, 1);

    // ---- Outgoing buffer -----------------------------------------------------
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 35);
    display.print("OUT");

    display.setColor(DisplayDriver::LIGHT);
    char outWithCursor[MORSE_OUT_BUF_LEN + 2];
    if (_outLen == 0) {
      strcpy(outWithCursor, "_");
    } else {
      strncpy(outWithCursor, _outBuf, sizeof(outWithCursor) - 2);
      outWithCursor[sizeof(outWithCursor) - 2] = 0;
      size_t n = strlen(outWithCursor);
      if (n < sizeof(outWithCursor) - 1) {
        outWithCursor[n] = '_';
        outWithCursor[n + 1] = 0;
      }
    }
    display.setCursor(0, 46);
    display.printWordWrap(outWithCursor, W);

    display.drawRect(0, 66, W, 1);

    // ---- Staging (current key sequence) + char count -------------------------
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 68);
    display.print("KEY");

    display.setColor(_exitArmed ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);
    display.setCursor(26, 68);
    display.print(_stagingLen > 0 ? _staging : " ");

    // Character count (right-aligned, same line)
    display.setColor(DisplayDriver::LIGHT);
    char ccBuf[12];
    snprintf(ccBuf, sizeof(ccBuf), "%u/%u", (unsigned)_outLen,
             (unsigned)(MORSE_OUT_BUF_LEN - 1));
    display.drawTextRightAlign(W - 1, 68, ccBuf);

    // WW/HH hint at bottom
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, 80);
    display.print("WW=send  HH=bksp");

    _dirty = false;
    _nextRender = millis();

    // E-ink refresh blocks the CPU for ~644ms. A short render interval
    // means the display re-renders almost continuously, starving poll()
    // and causing rapid button presses to be missed. 2s gives plenty of
    // time for the user to key a full letter between screen updates.
    return 2000;
  }
};

#endif  // MORSE_COMPOSE_ENABLED