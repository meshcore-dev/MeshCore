// Host tests for replyAppendf (src/helpers/MQTTReplyFormat.h), the bounded
// clamping printf-append used by MQTTBridge's status/stats/diag CLI formatters.
// Proves the A1 out-of-bounds-write bound holds regardless of input size, rather
// than "by input-size accident" (see the 2026-07-19 MQTT observer review).
#include <gtest/gtest.h>
#include <cstring>
#include "helpers/MQTTReplyFormat.h"

// Lay a canary past the logical buffer so any write at buf[bufsize..] is caught.
// `logical` bytes are the buffer handed to replyAppendf; the trailing GUARD bytes
// must stay 0xAA.
static const size_t GUARD = 8;
struct Canaried {
  static const size_t CAP = 256;
  char mem[CAP + GUARD];
  size_t logical;
  explicit Canaried(size_t n) : logical(n) {
    memset(mem, 0xAA, sizeof(mem));
    mem[0] = '\0';
  }
  char* buf() { return mem; }
  bool guardIntact() const {
    for (size_t i = logical; i < logical + GUARD; i++) {
      if ((unsigned char)mem[i] != 0xAA) return false;
    }
    return true;
  }
};

TEST(ReplyAppendf, BasicAppendWithinBounds) {
  char buf[64];
  int pos = 0;
  replyAppendf(buf, sizeof(buf), &pos, "> msgs: %s", "on");
  EXPECT_STREQ("> msgs: on", buf);
  EXPECT_EQ(pos, 10);
}

TEST(ReplyAppendf, SequenceAccumulates) {
  char buf[64];
  int pos = 0;
  replyAppendf(buf, sizeof(buf), &pos, "> msgs: %s", "off");
  replyAppendf(buf, sizeof(buf), &pos, ", %d: %s (%s)", 1, "denmesh", "ok");
  replyAppendf(buf, sizeof(buf), &pos, ", q:%d", 3);
  EXPECT_STREQ("> msgs: off, 1: denmesh (ok), q:3", buf);
  EXPECT_EQ(pos, (int)strlen(buf));
}

TEST(ReplyAppendf, TruncationClampsPosAndTerminates) {
  char buf[16];
  int pos = 0;
  replyAppendf(buf, sizeof(buf), &pos, "%s", "0123456789ABCDEF_TOO_LONG");
  // Written up to 15 chars + NUL; pos pinned at bufsize-1.
  EXPECT_EQ(pos, 15);
  EXPECT_EQ(buf[15], '\0');
  EXPECT_STREQ("0123456789ABCDE", buf);
}

TEST(ReplyAppendf, AppendAfterFullIsNoOp) {
  char buf[8];
  int pos = 0;
  replyAppendf(buf, sizeof(buf), &pos, "%s", "AAAAAAAAAAAA");  // overflows
  EXPECT_EQ(pos, 7);
  char snapshot[8];
  memcpy(snapshot, buf, sizeof(buf));
  // Further appends must not write anything and must keep pos clamped.
  replyAppendf(buf, sizeof(buf), &pos, ", more");
  replyAppendf(buf, sizeof(buf), &pos, ", q:%d", 9);
  EXPECT_EQ(pos, 7);
  EXPECT_EQ(0, memcmp(snapshot, buf, sizeof(buf)));
}

// The A1 reproduction: the old `pos += snprintf(...)` idiom would let pos exceed
// bufsize after a truncated append, so the *next* append wrote at buf+pos with a
// wrapped size_t length. replyAppendf must never touch the guard bytes.
TEST(ReplyAppendf, NoWritePastBufferAcrossOverflowingChain) {
  Canaried c(24);
  int pos = 0;
  // Mimic formatSlotDiagReply's chain, sized to blow well past 24 bytes.
  replyAppendf(c.buf(), c.logical, &pos, "> mqtt%d: %s", 6, "no client");
  replyAppendf(c.buf(), c.logical, &pos, ", dc:%lu", 4294967295UL);
  replyAppendf(c.buf(), c.logical, &pos, ", first_disc:%lus", 4294967295UL);
  replyAppendf(c.buf(), c.logical, &pos, ", %s (0x%04X)", "cert verify failed", 0x800Bu);
  replyAppendf(c.buf(), c.logical, &pos, ", mbedtls:-0x%04X", 0x8010u);
  replyAppendf(c.buf(), c.logical, &pos, ", sock:%d", -2147483647);
  replyAppendf(c.buf(), c.logical, &pos, ", %luh ago", 1193046UL);
  EXPECT_TRUE(c.guardIntact());
  EXPECT_LE(pos, (int)c.logical - 1);
  EXPECT_EQ(c.buf()[c.logical - 1], '\0');  // still NUL-terminated
}

TEST(ReplyAppendf, ExactFitBoundary) {
  char buf[11];  // room for exactly "0123456789" + NUL
  int pos = 0;
  replyAppendf(buf, sizeof(buf), &pos, "%s", "0123456789");
  EXPECT_EQ(pos, 10);
  EXPECT_STREQ("0123456789", buf);
  EXPECT_EQ(buf[10], '\0');
}

TEST(ReplyAppendf, NullAndZeroSizeAreNoOps) {
  int pos = 0;
  replyAppendf(nullptr, 16, &pos, "x");  // null buf
  EXPECT_EQ(pos, 0);
  char buf[8] = {'k', 0};
  replyAppendf(buf, 0, &pos, "x");       // zero size
  EXPECT_STREQ("k", buf);
  replyAppendf(buf, sizeof(buf), nullptr, "x");  // null pos
  EXPECT_STREQ("k", buf);
}

TEST(ReplyAppendf, BufsizeOneJustTerminates) {
  char buf[1];
  buf[0] = 'Z';
  int pos = 0;
  replyAppendf(buf, sizeof(buf), &pos, "anything");
  EXPECT_EQ(pos, 0);
  EXPECT_EQ(buf[0], '\0');
}

TEST(ReplyAppendf, NegativeIncomingPosTreatedAsZero) {
  char buf[16];
  memset(buf, 'x', sizeof(buf));
  int pos = -5;
  replyAppendf(buf, sizeof(buf), &pos, "hi");
  EXPECT_EQ(pos, 2);
  EXPECT_STREQ("hi", buf);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
