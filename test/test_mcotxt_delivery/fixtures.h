#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../../examples/companion_radio/MCOtxtDelivery.h"
#include "golden.h"
#include <helpers/Base91.h>
#include <helpers/mcotxt/MCOtxtCodec.h>
#include <helpers/mcotxt/MCOtxtDisplay.h>
#include <helpers/mcotxt/MCOtxtText.h>
#include <helpers/mcotxt/MCOtxtTransport.h>

namespace {

using Bytes = std::vector<uint8_t>;

Bytes fromHex(const char* hex) {
  Bytes result;
  for (size_t i = 0; hex[i]; i += 2) {
    const char byte[] = {hex[i], hex[i + 1], 0};
    result.push_back((uint8_t)strtoul(byte, nullptr, 16));
  }
  return result;
}

void integer(Bytes& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(value >> (8 * i)));
}

uint32_t timestamp(const Bytes& frame) {
  const size_t p = frame[0] == 17 ? 7 : 4;
  return (uint32_t)frame[p] | ((uint32_t)frame[p + 1] << 8) |
         ((uint32_t)frame[p + 2] << 16) | ((uint32_t)frame[p + 3] << 24);
}

void varuint(Bytes& out, size_t value) {
  do {
    out.push_back((uint8_t)((value & 0x7f) | (value > 0x7f ? 0x80 : 0)));
    value >>= 7;
  } while (value);
}

void stringField(Bytes& out, const std::string& value) {
  out.push_back(1);  // counted UTF-8 mode
  varuint(out, value.size());
  out.insert(out.end(), value.begin(), value.end());
}

Bytes container(const Golden& golden, const std::string& sender = "", const std::string& reply = "",
                bool inherited = true) {
  Bytes out = {0x31, (uint8_t)((inherited ? 4 : 0) | (sender.empty() ? 0 : 2) | (reply.empty() ? 0 : 1))};
  if (!inherited) integer(out, 0x12345678);
  if (!sender.empty()) stringField(out, sender);
  if (!reply.empty()) { stringField(out, reply); integer(out, 0x10203040); }
  out.push_back(0);  // MCOtxt string mode + Dart's counted frame
  const Bytes stream = fromHex(golden.frame_hex);
  out.insert(out.end(), stream.begin(), stream.end());
  return out;
}

std::string textToken(const Bytes& body) {
  char out[1024];
  size_t length;
  EXPECT_TRUE(mesh::base91::encode(body.data(), body.size(), out, sizeof(out), length));
  return std::string("mct:") + out;
}

Bytes channel(const std::string& text, bool v3 = true) {
  Bytes frame = v3 ? Bytes{17, 12, 0, 0} : Bytes{8};
  frame.insert(frame.end(), {2, 0x82, 0});  // channel index, packed path, plain
  integer(frame, 0x11223344);
  frame.insert(frame.end(), text.begin(), text.end());
  return frame;
}

Bytes direct(const std::string& text, uint8_t type = 0, bool v3 = true) {
  Bytes frame = v3 ? Bytes{16, 12, 0, 0} : Bytes{7};
  frame.insert(frame.end(), {1, 2, 3, 4, 5, 6, 0x81, type});
  integer(frame, 0x11223344);
  if (type == 2) frame.insert(frame.end(), {0xAA, 0xBB, 0xCC, 0xDD});
  frame.insert(frame.end(), text.begin(), text.end());
  return frame;
}

Bytes binary(const Bytes& body, const std::string& outer_name = "") {
  Bytes payload;
  varuint(payload, outer_name.size());
  payload.insert(payload.end(), outer_name.begin(), outer_name.end());
  payload.insert(payload.end(), body.begin(), body.end());
  Bytes frame = {27, 12, 0, 0, 2, 0x82, 0x20, 0x01, (uint8_t)payload.size()};
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

size_t headerSize(const Bytes& f) {
  switch (f[0]) {
    case 7: return 13 + (f[8] == 2 ? 4 : 0);
    case 8: return 8;
    case 16: return 16 + (f[11] == 2 ? 4 : 0);
    case 17: return 11;
    default: return 9;
  }
}

std::string bodyText(const Bytes& f) {
  const size_t offset = headerSize(f);
  return std::string(f.begin() + offset, f.end());
}

void start(MCOtxtDelivery& delivery, const std::string& info) {
  delivery.appStart((const uint8_t*)info.data(), info.size());
}

Bytes next(MCOtxtDelivery& delivery, const Bytes& source, bool& consume,
           uint8_t version = 4, uint32_t now = 100) {
  uint8_t guarded[MCOtxtDelivery::kFrameSize + 3];
  memset(guarded, 0xCD, sizeof(guarded));
  const int length = delivery.next(source.data(), source.size(), version, now, guarded + 1, consume);
  EXPECT_GE(length, 0);
  EXPECT_LE(length, (int)MCOtxtDelivery::kFrameSize);
  EXPECT_EQ(guarded[0], 0xCD);
  EXPECT_EQ(guarded[sizeof(guarded) - 1], 0xCD);
  return Bytes(guarded + 1, guarded + 1 + length);
}

std::vector<Bytes> drain(MCOtxtDelivery& delivery, const Bytes& source, uint8_t version = 4) {
  EXPECT_LE(source.size(), (size_t)MCOtxtDelivery::kFrameSize) << "fixture must fit the original offline queue";
  std::vector<Bytes> result;
  bool consume = false;
  for (int i = 0; i < 25 && !consume; ++i) {
    const Bytes before = source;
    const Bytes f = next(delivery, source, consume, version, 100 + i);
    EXPECT_EQ(source, before);  // raw queue bytes never change
    if (!f.empty()) result.push_back(f);
  }
  EXPECT_TRUE(consume);
  return result;
}

bool validUtf8(const std::string& text) {
  size_t pos = 0;
  uint32_t cp;
  while (pos < text.size()) {
    if (!mcotxt::utf8Decode((const uint8_t*)text.data(), text.size(), pos, cp)) return false;
  }
  return true;
}

// Reassemble without split decorations to detect dropped/repeated text.
std::string reassemble(const std::vector<Bytes>& frames, const std::string& prefix) {
  std::string result;
  for (size_t i = 0; i < frames.size(); ++i) {
    std::string text = bodyText(frames[i]);
    EXPECT_TRUE(validUtf8(text));
    EXPECT_LE(text.size(), (size_t)MCOtxtDelivery::kTextSize);
    EXPECT_EQ(text.substr(0, prefix.size()), prefix);
    text.erase(0, prefix.size());
    if (frames.size() > 1) {
      const std::string mark = " [" + std::to_string(i + 1) + "/" + std::to_string(frames.size()) + "]";
      EXPECT_EQ(text.substr(text.size() - mark.size()), mark);
      text.erase(text.size() - mark.size());
      if (i + 1 < frames.size()) {
        EXPECT_EQ(text.substr(text.size() - 3), "...");
        text.erase(text.size() - 3);
      }
      if (i > 0) { EXPECT_EQ(text.substr(0, 3), "..."); text.erase(0, 3); }
    }
    result += text;
  }
  return result;
}

std::string withoutSpaces(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
  return s;
}

}  // namespace

