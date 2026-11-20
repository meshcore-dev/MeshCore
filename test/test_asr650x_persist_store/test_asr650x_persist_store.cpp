#include "helpers/asr650x/FlashRecordStore.h"

#include <cstdio>
#include <cstring>
#include <gtest/gtest.h>

// RAM-backed fake of the SFLASH user rows: two 256-byte rows, a record per row.
struct FakeBackend {
  uint8_t mem[512];
  bool fail_next_write;
  size_t torn_bytes; // if > 0: only this many bytes of the next write land (simulated power loss)
  int writes;
  size_t last_off;
  FakeBackend() : fail_next_write(false), torn_bytes(0), writes(0), last_off(0) {
    memset(mem, 0xFF, sizeof(mem));
  }
  void read(size_t off, uint8_t *buf, size_t n) { memcpy(buf, mem + off, n); }
  bool write(size_t off, const uint8_t *buf, size_t n) {
    writes++;
    last_off = off;
    if (fail_next_write) {
      fail_next_write = false;
      return false;
    }
    if (torn_bytes) {
      memcpy(mem + off, buf, torn_bytes);
      torn_bytes = 0;
      return false;
    }
    memcpy(mem + off, buf, n);
    return true;
  }
};

static asr650x::PersistState with_identity(uint8_t fill) {
  asr650x::PersistState s;
  memset(&s, 0, sizeof(s));
  s.has_identity = true;
  memset(s.prv, fill, 64);
  memset(s.pub, fill ^ 0x5A, 32);
  return s;
}

TEST(Asr650xFlashRecordStore, Behaves) {
  FakeBackend be;
  asr650x::PersistStore<FakeBackend> st(be);
  st.begin();
  EXPECT_TRUE(!st.state().has_identity && !st.state().has_prefs);

  /* identity persists across a new store instance, alternating rows */
  EXPECT_TRUE(st.commit(with_identity(1)));
  EXPECT_TRUE(be.last_off == 0);
  asr650x::PersistState s2 = st.state();
  s2.has_prefs = true;
  strcpy(s2.node_name, "one");
  EXPECT_TRUE(st.commit(s2));
  EXPECT_TRUE(be.last_off == 256);
  {
    asr650x::PersistStore<FakeBackend> again(be);
    again.begin();
    EXPECT_TRUE(again.state().has_identity && again.state().prv[0] == 1);
    EXPECT_TRUE(again.state().has_prefs && strcmp(again.state().node_name, "one") == 0);
  }

  /* unchanged commit performs no write */
  int w = be.writes;
  EXPECT_TRUE(st.commit(st.state()));
  EXPECT_TRUE(be.writes == w);

  /* a failed commit must not change state, and must not leak into the next commit */
  asr650x::PersistState want_key2 = st.state();
  memset(want_key2.prv, 2, 64);
  be.fail_next_write = true;
  EXPECT_TRUE(!st.commit(want_key2));
  EXPECT_TRUE(st.state().prv[0] == 1);
  asr650x::PersistState rename = st.state();
  strcpy(rename.node_name, "two");
  EXPECT_TRUE(st.commit(rename));
  {
    asr650x::PersistStore<FakeBackend> again(be);
    again.begin();
    EXPECT_TRUE(again.state().prv[0] == 1); // the rejected key never reached flash
    EXPECT_TRUE(strcmp(again.state().node_name, "two") == 0);
  }

  /* power loss while writing the newer row keeps the previous record intact */
  asr650x::PersistState before = st.state();
  asr650x::PersistState next = st.state();
  strcpy(next.node_name, "three");
  be.torn_bytes = 70;
  EXPECT_TRUE(!st.commit(next));
  {
    asr650x::PersistStore<FakeBackend> again(be);
    again.begin();
    EXPECT_TRUE(again.state().has_identity && memcmp(again.state().prv, before.prv, 64) == 0);
    EXPECT_TRUE(strcmp(again.state().node_name, "two") == 0);
    /* and the store keeps working afterwards (writes the row that was torn) */
    EXPECT_TRUE(again.commit(next));
    asr650x::PersistStore<FakeBackend> third(be);
    third.begin();
    EXPECT_TRUE(strcmp(third.state().node_name, "three") == 0);
  }

  /* both rows corrupt -> empty */
  FakeBackend bad;
  memset(bad.mem, 0xA5, sizeof(bad.mem));
  asr650x::PersistStore<FakeBackend> sb(bad);
  sb.begin();
  EXPECT_TRUE(!sb.state().has_identity && !sb.state().has_prefs);
  EXPECT_TRUE(sb.commit(with_identity(9)));
  asr650x::PersistStore<FakeBackend> sb2(bad);
  sb2.begin();
  EXPECT_TRUE(sb2.state().has_identity && sb2.state().prv[0] == 9);

  /* sequence number wrap: 0 is newer than 0xFFFFFFFF */
  EXPECT_TRUE(asr650x::seqNewer(0u, 0xFFFFFFFFu));
  EXPECT_TRUE(!asr650x::seqNewer(0xFFFFFFFFu, 0u));
  EXPECT_TRUE(asr650x::seqNewer(5u, 4u) && !asr650x::seqNewer(4u, 5u) && !asr650x::seqNewer(7u, 7u));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
