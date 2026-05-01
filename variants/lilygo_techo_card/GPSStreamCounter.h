#pragma once

#include <Arduino.h>

// Transparent Stream wrapper that counts NMEA sentences (newline-delimited)
// flowing from the GPS serial port to the MicroNMEA parser.
//
// Usage: Instead of MicroNMEALocationProvider gps(Serial2, &rtc_clock);
//        Use: GPSStreamCounter gpsStream(Serial2);
//             MicroNMEALocationProvider gps(gpsStream, &rtc_clock);
//
// Every read() call passes through to the underlying stream; when a '\n'
// is seen the sentence counter increments. This lets the UI display a
// live "nmea" count so users can confirm the baud rate is correct and
// the GPS module is actually sending data.

class GPSStreamCounter : public Stream {
public:
  GPSStreamCounter(Stream& inner)
    : _inner(inner), _sentences(0), _sentences_snapshot(0),
      _last_snapshot(0), _sentences_per_sec(0) {}

  // --- Stream read interface (passes through) ---
  int available() override { return _inner.available(); }
  int peek() override { return _inner.peek(); }

  int read() override {
    int c = _inner.read();
    if (c == '\n') {
      _sentences++;
    }
    return c;
  }

  // --- Stream write interface (pass through for NMEA commands if needed) ---
  size_t write(uint8_t b) override { return _inner.write(b); }

  // Required override on Adafruit nRF52 BSP where Stream::flush() is pure virtual.
  // No-op equivalent on ESP32 cores that provide a default implementation.
  void flush() override { _inner.flush(); }

  // --- Sentence counting API ---

  // Total sentences received since boot (or last reset)
  uint32_t getSentenceCount() const { return _sentences; }

  // Sentences received per second (updated each time you call it,
  // with a 1-second rolling window)
  uint16_t getSentencesPerSec() {
    unsigned long now = millis();
    unsigned long elapsed = now - _last_snapshot;
    if (elapsed >= 1000) {
      uint32_t delta = _sentences - _sentences_snapshot;
      // Scale to per-second if interval wasn't exactly 1000ms
      _sentences_per_sec = (uint16_t)((delta * 1000UL) / elapsed);
      _sentences_snapshot = _sentences;
      _last_snapshot = now;
    }
    return _sentences_per_sec;
  }

  // Reset all counters (e.g. when GPS hardware power cycles)
  void resetCounters() {
    _sentences = 0;
    _sentences_snapshot = 0;
    _sentences_per_sec = 0;
    _last_snapshot = millis();
  }

private:
  Stream& _inner;
  volatile uint32_t _sentences;
  uint32_t _sentences_snapshot;
  unsigned long _last_snapshot;
  uint16_t _sentences_per_sec;
};
