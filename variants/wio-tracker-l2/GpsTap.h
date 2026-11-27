#pragma once

#include <Arduino.h>

// Transparent wrapper around the GNSS UART. MicroNMEA ignores GSV sentences,
// so this watches the byte stream for them and keeps a running count of
// satellites in view, strong satellites and best SNR, which separates RF
// faults from a receiver that only lacks sky view. It also records the
// fix flags straight from GGA/RMC so a parser problem upstream can't hide a
// real fix, and can hand every complete line to a logger (SD card).
class GpsTapStream : public Stream {
  Stream& _src;
  char _line[120];
  uint8_t _len = 0;
  struct Talker { char id[3]; uint8_t in_view; uint8_t strong; uint8_t strong_acc; unsigned long at; };
  Talker _talkers[6] = {};
  int _best_snr = 0;
  unsigned long _best_at = 0;
  unsigned long _last_gsv = 0;
  int _gga_fix = -1;        // GGA fix quality (0 = no fix)
  int _gga_used = 0;        // GGA satellites used
  char _rmc_status = '?';   // RMC status: A = valid, V = void
  unsigned long _last_fix_sentence = 0;

  static const unsigned long FRESH_MS = 6000;
  static const int STRONG_DB = 30;

  // pointer to comma-separated field n (0 = sentence id), NULL if absent
  static const char* field(const char* s, int n) {
    const char* p = s;
    for (int i = 0; i < n; i++) {
      p = strchr(p, ',');
      if (p == NULL) return NULL;
      p++;
    }
    return p;
  }

  void parseGsv(const char* s) {
    // $GPGSV,total,msg,in_view,prn,elev,az,snr,...*cs
    char talker[3] = {s[1], s[2], 0};
    const char* f;
    int total = (f = field(s, 1)) ? atoi(f) : 0;
    int msg   = (f = field(s, 2)) ? atoi(f) : 0;
    int in_view = (f = field(s, 3)) ? atoi(f) : -1;
    if (in_view < 0) return;

    int strong_here = 0;
    for (int k = 0; k < 4; k++) {
      const char* snr = field(s, 7 + 4 * k);
      if (snr == NULL || *snr == ',' || *snr == '*' || *snr == 0) continue;
      int v = atoi(snr);
      if (v >= STRONG_DB) strong_here++;
      if (v > _best_snr || millis() - _best_at > FRESH_MS) { _best_snr = v; _best_at = millis(); }
    }

    int slot = -1;
    for (int i = 0; i < 6; i++) {
      if (strcmp(_talkers[i].id, talker) == 0) { slot = i; break; }
      if (slot < 0 && _talkers[i].id[0] == 0) slot = i;
    }
    if (slot < 0) return;
    Talker& t = _talkers[slot];
    memcpy(t.id, talker, 3);
    t.in_view = (uint8_t) in_view;
    if (msg <= 1) t.strong_acc = 0;
    t.strong_acc += strong_here;
    if (msg >= total) t.strong = t.strong_acc;
    t.at = millis();
    _last_gsv = millis();
  }

  void parseFixFlags(const char* s) {
    const char* type = s + 3;
    if (strncmp(type, "GGA", 3) == 0) {
      const char* f;
      _gga_fix  = (f = field(s, 6)) && *f != ',' ? atoi(f) : 0;
      _gga_used = (f = field(s, 7)) && *f != ',' ? atoi(f) : 0;
      _last_fix_sentence = millis();
    } else if (strncmp(type, "RMC", 3) == 0) {
      const char* f = field(s, 2);
      _rmc_status = (f != NULL && *f != ',' && *f != 0) ? *f : '?';
      _last_fix_sentence = millis();
    }
  }

  void feed(int c) {
    if (echo) Serial.write((uint8_t) c);
    if (c == '\n' || c == '\r') {
      if (_len > 6 && _line[0] == '$') {
        _line[_len] = 0;
        if (on_line != NULL) on_line(_line);
        if (strncmp(_line + 3, "GSV", 3) == 0) parseGsv(_line);
        else parseFixFlags(_line);
      }
      _len = 0;
    } else if (_len < sizeof(_line) - 1) {
      _line[_len++] = (char) c;
    }
  }

public:
  bool echo = false;                          // mirror raw NMEA to USB Serial
  void (*on_line)(const char* line) = NULL;   // complete-sentence hook (e.g. SD log)

  explicit GpsTapStream(Stream& src) : _src(src) {}

  int satsInView() const {
    int n = 0;
    for (int i = 0; i < 6; i++) {
      if (_talkers[i].id[0] != 0 && millis() - _talkers[i].at < FRESH_MS) n += _talkers[i].in_view;
    }
    return n;
  }
  int strongSats() const {
    int n = 0;
    for (int i = 0; i < 6; i++) {
      if (_talkers[i].id[0] != 0 && millis() - _talkers[i].at < FRESH_MS) n += _talkers[i].strong;
    }
    return n;
  }
  int  bestSnr() const { return millis() - _best_at < FRESH_MS ? _best_snr : 0; }
  bool streaming() const { return millis() - _last_gsv < FRESH_MS; }
  int  ggaFix() const { return _gga_fix; }
  int  ggaUsed() const { return _gga_used; }
  char rmcStatus() const { return _rmc_status; }

  // Stream interface: pass-through with a tap on read()
  int available() override { return _src.available(); }
  int peek() override { return _src.peek(); }
  int read() override {
    int c = _src.read();
    if (c >= 0) feed(c);
    return c;
  }
  void flush() override { _src.flush(); }
  size_t write(uint8_t b) override { return _src.write(b); }
  size_t write(const uint8_t* buf, size_t n) override { return _src.write(buf, n); }
};
