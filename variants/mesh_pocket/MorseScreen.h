#pragma once

// =============================================================================
// MorseScreen — single-button Morse compose/receive for the Meshpocket
//
// Entered from the home screen via a double-click on the USER button when
// MORSE_COMPOSE_ENABLED is defined (Meshpocket companion builds only).
//
// While active, this screen takes exclusive ownership of the USER button:
// - Short press  -> dot (<500 ms)
// - Medium press -> dash (500 ms – 3 s)
// - Hold 3–7 s   -> BACKSPACE (deletes last character)
// - Hold 7–9 s   -> SEND to Public channel, clear buffer
// - Hold 9 s+    -> EXIT back to home screen
// - Letter gap (~1 s silence) commits the staged pattern to the buffer
// - Word gap (~2 s silence) inserts a space
// - `WW` prosign (.--.--)  -> send (alternative to hold)
// - `HH` prosign (........) -> backspace (alternative to hold)
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
// Tunables — calibrated for single momentary button on e-ink device.
// Standard Morse timing is too tight for a button (vs a proper key) and
// e-ink renders block the CPU for ~644ms, so generous thresholds are needed.
// -----------------------------------------------------------------------------

#define MORSE_DOT_UNIT_MS      120

// Dot/dash threshold. 500ms gives a comfortable margin — a quick tap is
// unmistakably a dot, a deliberate half-second hold is a dash.
#define MORSE_DOT_DASH_MS      500

// Letter commit gap. 1s silence after the last release commits the staged
// pattern. This matches the user's natural pause between letters.
#define MORSE_LETTER_GAP_MS    1000

// Word space gap. 3.5s silence inserts a space. Well above natural
// inter-letter pauses (typically 1–2s) to avoid unwanted spaces.
#define MORSE_WORD_GAP_MS      3500

// Hold-duration actions — release at the right moment.
// Wide spacing between thresholds prevents accidental triggers.
#define MORSE_BACKSPACE_HOLD_MS  3000
#define MORSE_SEND_HOLD_MS       7000
#define MORSE_EXIT_HOLD_MS       9000

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

// Hold action states
enum HoldAction : uint8_t {
  HOLD_NONE = 0,
  HOLD_BACKSPACE,
  HOLD_SEND,
  HOLD_EXIT
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
  unsigned long _releaseAt;
  bool          _letterDecoded;
  bool          _wordSpaceInserted;
  HoldAction    _holdAction;

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
      Serial.printf("[MORSE] decoded \"%s\" -> WW (SEND), outLen=%d\n", _staging, _outLen);
      if (_outLen > 0) _wantsSend = true;
    } else if (decoded == '\x02') {
      Serial.printf("[MORSE] decoded \"%s\" -> HH (BACKSPACE)\n", _staging);
      if (_outLen > 0) {
        _outLen--;
        _outBuf[_outLen] = 0;
      }
    } else if (decoded != 0) {
      // Convert to lowercase — Morse table produces uppercase but lowercase
      // reads more naturally in chat messages
      if (decoded >= 'A' && decoded <= 'Z') decoded += 32;
      Serial.printf("[MORSE] decoded \"%s\" -> '%c'\n", _staging, decoded);
      if (_outLen < MORSE_OUT_BUF_LEN - 1) {
        _outBuf[_outLen++] = decoded;
        _outBuf[_outLen] = 0;
      }
    } else {
      Serial.printf("[MORSE] decoded \"%s\" -> NO MATCH (dropped)\n", _staging);
    }
    Serial.printf("[MORSE] outBuf: \"%s\" (%d chars)\n", _outBuf, _outLen);
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

  void doBackspace() {
    _stagingLen = 0;
    _staging[0] = 0;
    if (_outLen > 0) {
      _outLen--;
      _outBuf[_outLen] = 0;
    }
    _dirty = true;
  }

public:
  MorseScreen(mesh::RTCClock* rtc)
    : _rtc(rtc),
      _outLen(0), _stagingLen(0),
      _btnPrevPressed(false), _pressStart(0), _releaseAt(0),
      _letterDecoded(false), _wordSpaceInserted(false),
      _holdAction(HOLD_NONE),
      _wantsExit(false), _wantsSend(false),
      _inboxNewest(0), _inboxCount(0),
      _dirty(true), _nextRender(0)
  {
    _outBuf[0] = 0;
    _staging[0] = 0;
    memset(_inbox, 0, sizeof(_inbox));
  }

  // Called by UITask when the screen is activated (on double-click from home)
  // Resets composition state so each session starts clean.
  void activate() {
    _outLen = 0;       _outBuf[0] = 0;
    _stagingLen = 0;   _staging[0] = 0;
    _btnPrevPressed = user_btn.isPressed();
    _pressStart = 0;
    _releaseAt = 0;
    _letterDecoded = false;
    _wordSpaceInserted = false;
    _holdAction = HOLD_NONE;
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
      // ---- Edge: released -> pressed ----
      _pressStart = now;
      _holdAction = HOLD_NONE;
      _letterDecoded = false;
      _wordSpaceInserted = false;
      Serial.println("[MORSE] btn DOWN");

    } else if (!pressed && _btnPrevPressed) {
      // ---- Edge: pressed -> released ----
      unsigned long dur = now - _pressStart;
      switch (_holdAction) {
        case HOLD_EXIT:
          Serial.printf("[MORSE] btn UP after %lums — EXIT\n", dur);
          _wantsExit = true;
          break;
        case HOLD_SEND:
          Serial.printf("[MORSE] btn UP after %lums — SEND, outLen=%d\n", dur, _outLen);
          if (_outLen > 0) _wantsSend = true;
          break;
        case HOLD_BACKSPACE:
          Serial.printf("[MORSE] btn UP after %lums — BACKSPACE\n", dur);
          doBackspace();
          break;
        default: {
          // Normal dot/dash
          char sym = (dur < MORSE_DOT_DASH_MS) ? '.' : '-';
          Serial.printf("[MORSE] btn UP after %lums — %s (%c)\n", dur,
                        sym == '.' ? "DOT" : "DASH", sym);
          if (_stagingLen < MORSE_STAGING_MAX - 1) {
            _staging[_stagingLen++] = sym;
            _staging[_stagingLen] = 0;
          }
          Serial.printf("[MORSE] staging now: \"%s\" (%d elements)\n", _staging, _stagingLen);
          _releaseAt = now;
          _dirty = true;
          break;
        }
      }
      _holdAction = HOLD_NONE;

    } else if (pressed && _btnPrevPressed) {
      // ---- Still holding — update armed action ----
      unsigned long dur = now - _pressStart;
      HoldAction newAction;
      if (dur >= MORSE_EXIT_HOLD_MS) {
        newAction = HOLD_EXIT;
      } else if (dur >= MORSE_SEND_HOLD_MS) {
        newAction = HOLD_SEND;
      } else if (dur >= MORSE_BACKSPACE_HOLD_MS) {
        newAction = HOLD_BACKSPACE;
      } else {
        newAction = HOLD_NONE;
      }
      if (newAction != _holdAction) {
        Serial.printf("[MORSE] hold %lums — armed: %s\n", dur,
                      newAction == HOLD_BACKSPACE ? "BKSP" :
                      newAction == HOLD_SEND ? "SEND" :
                      newAction == HOLD_EXIT ? "EXIT" : "none");
        _holdAction = newAction;
        _dirty = true;
      }

    } else {
      // ---- Idle — check gap timers ----
      if (_stagingLen > 0 && _releaseAt > 0
          && (now - _releaseAt) >= MORSE_LETTER_GAP_MS) {
        Serial.printf("[MORSE] letter gap %lums — committing \"%s\"\n",
                      now - _releaseAt, _staging);
        commitStaging();
        _releaseAt = now;
      } else if (_outLen > 0 && _letterDecoded && !_wordSpaceInserted
                 && _releaseAt > 0
                 && (now - _releaseAt) >= MORSE_WORD_GAP_MS) {
        Serial.printf("[MORSE] word gap %lums — inserting space\n", now - _releaseAt);
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

    // Show armed hold action in header
    if (_holdAction != HOLD_NONE) {
      display.setColor(DisplayDriver::GREEN);
      const char* action =
        _holdAction == HOLD_BACKSPACE ? "[BKSP]" :
        _holdAction == HOLD_SEND     ? "[SEND]" :
                                       "[EXIT]";
      display.drawTextRightAlign(W - 1, 0, action);
    }

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

    // ---- Staging + char count ------------------------------------------------
    // CRITICAL: The KEY area must NOT change CRC during active dot/dash input.
    // Any CRC change triggers a 644ms e-ink block that eats button presses.
    // Only hold actions (3s+) change the display here — by then the user has
    // stopped rapid-pressing so one render block is harmless.
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 68);
    display.print("KEY");

    display.setCursor(26, 68);
    if (_holdAction != HOLD_NONE) {
      display.setColor(DisplayDriver::YELLOW);
      const char* action =
        _holdAction == HOLD_BACKSPACE ? "[BKSP]" :
        _holdAction == HOLD_SEND     ? "[SEND]" :
                                       "[EXIT]";
      display.print(action);
    } else {
      display.setColor(DisplayDriver::LIGHT);
      display.print("ready");
    }

    // Character count (right-aligned, same line)
    display.setColor(DisplayDriver::LIGHT);
    char ccBuf[12];
    snprintf(ccBuf, sizeof(ccBuf), "%u/%u", (unsigned)_outLen,
             (unsigned)(MORSE_OUT_BUF_LEN - 1));
    display.drawTextRightAlign(W - 1, 68, ccBuf);

    // WW/HH hint at bottom
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, 80);
    display.print("Hold 3s=bksp 7s=send 9s=exit");

    _dirty = false;
    _nextRender = millis();

    // T-Deck Pro render throttle pattern: 800ms minimum after endFrame()
    // guarantees unblocked poll() time for button sampling. The CRC check
    // in endFrame() means renders only block (~644ms) when content actually
    // changed — unchanged frames return instantly regardless of interval.
    return 800;
  }
};

#endif  // MORSE_COMPOSE_ENABLED