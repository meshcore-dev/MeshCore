#pragma once

// Shared helpers for the native GPS/NMEA tests: the real
// MicroNMEALocationProvider and MicroNMEA, fed through a mock UART.

#include <gtest/gtest.h>

#include <MicroNMEA.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>


struct MockSerial : Stream {
  std::deque<char> queue;
  int available() override { return (int)queue.size(); }
  int read() override {
    if (queue.empty()) return -1;
    char c = queue.front();
    queue.pop_front();
    return (unsigned char)c;
  }
  void feed(const std::string& text) {
    for (char c : text) queue.push_back(c);
  }
};

// "$" + body + "*" + two hex digits, the checksum MicroNMEA expects.
inline std::string withChecksum(const std::string& body) {
  uint8_t sum = 0;
  for (char c : body) sum ^= (uint8_t)c;
  char hex[4];
  snprintf(hex, sizeof(hex), "%02X", sum);
  return "$" + body + "*" + hex;
}

inline void feedAndLoop(MicroNMEALocationProvider& gps, MockSerial& serial,
                 const std::string& line) {
  serial.feed(line + "\r\n");
  gps.loop();
}

inline constexpr const char* kGoodRmc =
    "GPRMC,120034.00,A,4716.1234,N,12233.5678,W,0.12,34.5,171026,003.1,W,A";
inline constexpr const char* kGoodGga =
    "GPGGA,120034.00,4716.1234,N,12233.5678,W,1,08,0.9,125.0,M,-17.0,M,,";
