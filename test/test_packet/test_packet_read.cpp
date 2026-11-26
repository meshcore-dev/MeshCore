#include "Packet.h"

#include <cstring>
#include <gtest/gtest.h>

using namespace mesh;

#define SENTINEL 0xEE

// A buffer that is longer than the 'len' handed to readFrom(), filled with a
// sentinel. If readFrom() copies bytes from beyond 'len', they show up in the Packet.
static void fillSentinel(uint8_t *buf, size_t size) {
  memset(buf, SENTINEL, size);
}

static bool contains(const uint8_t *buf, size_t size, uint8_t value) {
  for (size_t i = 0; i < size; i++) {
    if (buf[i] == value) return true;
  }
  return false;
}

// ── valid packets ────────────────────────────────────────────────────────────

TEST(PacketReadFrom, RoundTrip_FloodWithPath) {
  Packet a;
  a.header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
  a.path_len = 3;
  a.path[0] = 0x11;
  a.path[1] = 0x22;
  a.path[2] = 0x33;
  a.payload_len = 5;
  memcpy(a.payload, "hello", 5);

  uint8_t raw[MAX_TRANS_UNIT];
  uint8_t len = a.writeTo(raw);

  Packet b;
  ASSERT_TRUE(b.readFrom(raw, len));
  EXPECT_EQ(a.header, b.header);
  EXPECT_EQ(3, b.path_len);
  EXPECT_EQ(0, memcmp(a.path, b.path, 3));
  EXPECT_EQ(5, b.payload_len);
  EXPECT_EQ(0, memcmp("hello", b.payload, 5));
}

TEST(PacketReadFrom, RoundTrip_TransportCodes) {
  Packet a;
  a.header = ROUTE_TYPE_TRANSPORT_FLOOD | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
  a.transport_codes[0] = 0x1234;
  a.transport_codes[1] = 0x5678;
  a.path_len = 0;
  a.payload_len = 1;
  a.payload[0] = 0x42;

  uint8_t raw[MAX_TRANS_UNIT];
  uint8_t len = a.writeTo(raw);

  Packet b;
  ASSERT_TRUE(b.readFrom(raw, len));
  EXPECT_EQ(0x1234, b.transport_codes[0]);
  EXPECT_EQ(0x5678, b.transport_codes[1]);
  EXPECT_EQ(1, b.payload_len);
  EXPECT_EQ(0x42, b.payload[0]);
}

// ── truncated input is rejected ──────────────────────────────────────────────

TEST(PacketReadFrom, RejectsEmptyAndHeaderOnly) {
  uint8_t raw[8] = { ROUTE_TYPE_FLOOD, 0, 0x42 };
  Packet p;
  EXPECT_FALSE(p.readFrom(raw, 0));
  EXPECT_FALSE(p.readFrom(raw, 1));
}

TEST(PacketReadFrom, RejectsPathWithNoPayload) {
  uint8_t raw[] = { ROUTE_TYPE_FLOOD, 2, 0x11, 0x22 }; // header, path_len, 2 path bytes, nothing else
  Packet p;
  EXPECT_FALSE(p.readFrom(raw, sizeof(raw)));
}

// ── truncated input is not read past 'len' ───────────────────────────────────

TEST(PacketReadFrom, TruncatedPath_DoesNotReadPastLen) {
  uint8_t raw[MAX_TRANS_UNIT];
  fillSentinel(raw, sizeof(raw));
  raw[0] = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
  raw[1] = 60;   // claims 60 path bytes
  raw[2] = 0x11; // ... but only one is inside 'len'

  Packet p;
  memset(p.path, 0, sizeof(p.path));
  EXPECT_FALSE(p.readFrom(raw, 3));
  EXPECT_FALSE(contains(p.path, sizeof(p.path), SENTINEL));
}

TEST(PacketReadFrom, TruncatedTransportCodes_DoesNotReadPastLen) {
  uint8_t raw[MAX_TRANS_UNIT];
  fillSentinel(raw, sizeof(raw));
  raw[0] = ROUTE_TYPE_TRANSPORT_FLOOD | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);

  Packet p;
  p.transport_codes[0] = p.transport_codes[1] = 0;
  for (uint8_t len = 1; len <= 5; len++) { // header + 4 code bytes + path_len = 6 needed
    EXPECT_FALSE(p.readFrom(raw, len)) << "len=" << (int)len;
  }
  EXPECT_EQ(0, p.transport_codes[0]);
  EXPECT_EQ(0, p.transport_codes[1]);
}
