// Host-side unit tests for TrustedClockTable.
// Runs under PlatformIO's native env: `pio test -e native` (or `make test`).

#include <unity.h>
#include <cstdio>
#include <cstring>

#include "TrustedClockTable.h"

void setUp(void)    {}
void tearDown(void) {}

static void fill_key(uint8_t* k, uint8_t seed) {
  for (int i = 0; i < PUB_KEY_SIZE; i++) k[i] = (uint8_t)(seed + i);
}

static void test_find_add_remove(void) {
  TrustedClockTable t;
  uint8_t k1[PUB_KEY_SIZE]; fill_key(k1, 1);
  uint8_t k2[PUB_KEY_SIZE]; fill_key(k2, 2);

  TEST_ASSERT_EQUAL_UINT8(0, t.count());
  TEST_ASSERT_NULL(t.find(k1));
  TEST_ASSERT_NULL(t.findByName("alice"));
  TEST_ASSERT_NULL(t.findByName(NULL));
  TEST_ASSERT_NULL(t.findByName(""));

  TEST_ASSERT_TRUE(t.add(k1, "alice"));
  TEST_ASSERT_EQUAL_UINT8(1, t.count());
  TrustedClockSource* s1 = t.find(k1);
  TEST_ASSERT_NOT_NULL(s1);
  TEST_ASSERT_EQUAL_STRING("alice", s1->name);
  TEST_ASSERT_EQUAL_UINT32(0, s1->last_timestamp);
  TEST_ASSERT_EQUAL_PTR(s1, t.findByName("alice"));

  // Re-add same key with new name updates name; count unchanged.
  TEST_ASSERT_TRUE(t.add(k1, "alice2"));
  TEST_ASSERT_EQUAL_UINT8(1, t.count());
  TEST_ASSERT_EQUAL_STRING("alice2", t.find(k1)->name);

  // Re-add same key with NULL name leaves existing name.
  TEST_ASSERT_TRUE(t.add(k1, NULL));
  TEST_ASSERT_EQUAL_STRING("alice2", t.find(k1)->name);

  // Add a second key with no name.
  TEST_ASSERT_TRUE(t.add(k2, NULL));
  TEST_ASSERT_EQUAL_UINT8(2, t.count());
  TEST_ASSERT_EQUAL_INT(0, t.find(k2)->name[0]);

  // Remove first.
  TEST_ASSERT_TRUE(t.remove(k1));
  TEST_ASSERT_EQUAL_UINT8(1, t.count());
  TEST_ASSERT_NULL(t.find(k1));
  TEST_ASSERT_NOT_NULL(t.find(k2));

  // Remove again returns false.
  TEST_ASSERT_FALSE(t.remove(k1));
}

static void test_capacity(void) {
  TrustedClockTable t;
  uint8_t k[PUB_KEY_SIZE];
  for (int i = 0; i < MAX_TRUSTED_CLOCK_KEYS; i++) {
    fill_key(k, (uint8_t)(10 + i));
    char name[8]; snprintf(name, sizeof(name), "n%d", i);
    TEST_ASSERT_TRUE(t.add(k, name));
  }
  TEST_ASSERT_EQUAL_UINT8(MAX_TRUSTED_CLOCK_KEYS, t.count());
  TEST_ASSERT_TRUE(t.isFull());

  // One more new key — must fail.
  fill_key(k, 99);
  TEST_ASSERT_FALSE(t.add(k, "overflow"));
  TEST_ASSERT_EQUAL_UINT8(MAX_TRUSTED_CLOCK_KEYS, t.count());

  // Re-adding an existing key is still fine when full.
  uint8_t existing[PUB_KEY_SIZE]; fill_key(existing, 10);
  TEST_ASSERT_TRUE(t.add(existing, "renamed"));
  TEST_ASSERT_EQUAL_STRING("renamed", t.find(existing)->name);
}

static void test_remove_shifts(void) {
  TrustedClockTable t;
  uint8_t k0[PUB_KEY_SIZE]; fill_key(k0, 1);
  uint8_t k1[PUB_KEY_SIZE]; fill_key(k1, 2);
  uint8_t k2[PUB_KEY_SIZE]; fill_key(k2, 3);
  t.add(k0, "a"); t.add(k1, "b"); t.add(k2, "c");

  // Remove middle; remaining order should be a, c.
  TEST_ASSERT_TRUE(t.remove(k1));
  TEST_ASSERT_EQUAL_UINT8(2, t.count());
  TEST_ASSERT_EQUAL_STRING("a", t.at(0).name);
  TEST_ASSERT_EQUAL_STRING("c", t.at(1).name);
}

static void test_predicates(void) {
  // passesReplay: strictly newer.
  TEST_ASSERT_TRUE (TrustedClockTable::passesReplay(100, 99));
  TEST_ASSERT_FALSE(TrustedClockTable::passesReplay(100, 100));
  TEST_ASSERT_FALSE(TrustedClockTable::passesReplay(100, 101));
  TEST_ASSERT_TRUE (TrustedClockTable::passesReplay(1, 0));

  // absDiff: order-independent.
  TEST_ASSERT_EQUAL_UINT32(10, TrustedClockTable::absDiff(100, 90));
  TEST_ASSERT_EQUAL_UINT32(10, TrustedClockTable::absDiff(90, 100));
  TEST_ASSERT_EQUAL_UINT32(0,  TrustedClockTable::absDiff(7, 7));

  // shouldStep.
  TEST_ASSERT_FALSE(TrustedClockTable::shouldStep(1000, 1100, 0));    // disabled
  TEST_ASSERT_FALSE(TrustedClockTable::shouldStep(1000, 1059, 60));   // diff < threshold
  TEST_ASSERT_TRUE (TrustedClockTable::shouldStep(1000, 1060, 60));   // boundary: >=
  TEST_ASSERT_TRUE (TrustedClockTable::shouldStep(1000, 940,  60));   // past, boundary
  TEST_ASSERT_FALSE(TrustedClockTable::shouldStep(1000, 941,  60));   // past, just under
  TEST_ASSERT_FALSE(TrustedClockTable::shouldStep(1000, 1000, 1));    // exactly equal
}

static void test_sync_event_ring(void) {
  TrustedClockTable t;
  uint8_t k[PUB_KEY_SIZE]; fill_key(k, 5);

  TEST_ASSERT_EQUAL_UINT8(0, t.syncEventCount());

  t.recordSyncEvent(k, "src1", 1000, 1100);
  TEST_ASSERT_EQUAL_UINT8(1, t.syncEventCount());
  TEST_ASSERT_EQUAL_UINT32(1100, t.syncEventNewestFirst(0).advert_timestamp);
  TEST_ASSERT_EQUAL_UINT32(1000, t.syncEventNewestFirst(0).local_before);
  TEST_ASSERT_EQUAL_STRING("src1", t.syncEventNewestFirst(0).name);

  t.recordSyncEvent(k, "src2", 2000, 2050);
  TEST_ASSERT_EQUAL_UINT8(2, t.syncEventCount());                       // ring full at MAX_SYNC_EVENTS
  TEST_ASSERT_EQUAL_UINT32(2050, t.syncEventNewestFirst(0).advert_timestamp);
  TEST_ASSERT_EQUAL_UINT32(1100, t.syncEventNewestFirst(1).advert_timestamp);

  // Third write evicts the oldest, preserves newest-first order.
  t.recordSyncEvent(k, "src3", 3000, 3030);
  TEST_ASSERT_EQUAL_UINT8(MAX_SYNC_EVENTS, t.syncEventCount());
  TEST_ASSERT_EQUAL_UINT32(3030, t.syncEventNewestFirst(0).advert_timestamp);
  TEST_ASSERT_EQUAL_UINT32(2050, t.syncEventNewestFirst(1).advert_timestamp);
}

static void test_serialize_roundtrip(void) {
  TrustedClockTable a;
  uint8_t k1[PUB_KEY_SIZE]; fill_key(k1, 1);
  uint8_t k2[PUB_KEY_SIZE]; fill_key(k2, 2);
  a.add(k1, "first");
  a.add(k2, "second");

  uint8_t buf[256];
  size_t n = a.serialize(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_size_t(TrustedClockTable::HEADER_SIZE + 2 * TrustedClockTable::RECORD_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(TrustedClockTable::FORMAT_VERSION, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(2, buf[1]);

  TrustedClockTable b;
  TEST_ASSERT_TRUE(b.deserialize(buf, n));
  TEST_ASSERT_EQUAL_UINT8(2, b.count());
  TEST_ASSERT_EQUAL_MEMORY(k1, b.at(0).pub_key, PUB_KEY_SIZE);
  TEST_ASSERT_EQUAL_MEMORY(k2, b.at(1).pub_key, PUB_KEY_SIZE);
  TEST_ASSERT_EQUAL_STRING("first",  b.at(0).name);
  TEST_ASSERT_EQUAL_STRING("second", b.at(1).name);
  // last_timestamp is intentionally not persisted; both should be 0 after load.
  TEST_ASSERT_EQUAL_UINT32(0, b.at(0).last_timestamp);
  TEST_ASSERT_EQUAL_UINT32(0, b.at(1).last_timestamp);
}

static void test_serialize_too_small_buffer(void) {
  TrustedClockTable a;
  uint8_t k[PUB_KEY_SIZE]; fill_key(k, 7);
  a.add(k, "x");
  uint8_t tiny[1];
  TEST_ASSERT_EQUAL_size_t(0, a.serialize(tiny, sizeof(tiny)));   // refuses partial write
}

static void test_deserialize_bad_version(void) {
  TrustedClockTable t;
  uint8_t k[PUB_KEY_SIZE]; fill_key(k, 8);
  t.add(k, "preexisting");

  uint8_t bad[2] = { 0xFF, 0 };
  TEST_ASSERT_FALSE(t.deserialize(bad, sizeof(bad)));
  // deserialize() resets the table even on failure (matches load semantics).
  TEST_ASSERT_EQUAL_UINT8(0, t.count());
}

static void test_deserialize_clamps_count(void) {
  // Buffer claims more entries than MAX_TRUSTED_CLOCK_KEYS; loader must clamp.
  size_t cap = TrustedClockTable::HEADER_SIZE
               + (size_t)(MAX_TRUSTED_CLOCK_KEYS + 2) * TrustedClockTable::RECORD_SIZE;
  uint8_t buf[512];
  TEST_ASSERT_TRUE(cap <= sizeof(buf));
  buf[0] = TrustedClockTable::FORMAT_VERSION;
  buf[1] = MAX_TRUSTED_CLOCK_KEYS + 2;
  size_t off = TrustedClockTable::HEADER_SIZE;
  for (int i = 0; i < MAX_TRUSTED_CLOCK_KEYS + 2; i++) {
    for (int j = 0; j < PUB_KEY_SIZE; j++) buf[off + j] = (uint8_t)(i * 10 + j);
    off += PUB_KEY_SIZE;
    char name[TRUSTED_CLOCK_NAME_LEN] = {0};
    snprintf(name, sizeof(name), "n%d", i);
    memcpy(buf + off, name, TRUSTED_CLOCK_NAME_LEN);
    off += TRUSTED_CLOCK_NAME_LEN;
  }

  TrustedClockTable t;
  TEST_ASSERT_TRUE(t.deserialize(buf, off));
  TEST_ASSERT_EQUAL_UINT8(MAX_TRUSTED_CLOCK_KEYS, t.count());
}

static void test_deserialize_truncated_records(void) {
  // Header says 3 records but the buffer only holds 1 full record.
  uint8_t k[PUB_KEY_SIZE]; fill_key(k, 9);
  uint8_t buf[256];
  buf[0] = TrustedClockTable::FORMAT_VERSION;
  buf[1] = 3;
  memcpy(buf + 2, k, PUB_KEY_SIZE);
  memset(buf + 2 + PUB_KEY_SIZE, 0, TRUSTED_CLOCK_NAME_LEN);
  memcpy(buf + 2 + PUB_KEY_SIZE, "trunc", 5);
  size_t len = TrustedClockTable::HEADER_SIZE + TrustedClockTable::RECORD_SIZE;

  TrustedClockTable t;
  TEST_ASSERT_TRUE(t.deserialize(buf, len));
  TEST_ASSERT_EQUAL_UINT8(1, t.count());
  TEST_ASSERT_EQUAL_STRING("trunc", t.at(0).name);
}

static void test_name_length_clamping(void) {
  TrustedClockTable t;
  uint8_t k[PUB_KEY_SIZE]; fill_key(k, 11);
  const char* longname = "0123456789ABCDEFG_overflow_extra";
  TEST_ASSERT_TRUE(t.add(k, longname));
  const TrustedClockSource& s = t.at(0);
  TEST_ASSERT_EQUAL_INT(0, s.name[TRUSTED_CLOCK_NAME_LEN - 1]);
  TEST_ASSERT_TRUE(strlen(s.name) < TRUSTED_CLOCK_NAME_LEN);
  TEST_ASSERT_EQUAL_MEMORY(longname, s.name, strlen(s.name));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_find_add_remove);
  RUN_TEST(test_capacity);
  RUN_TEST(test_remove_shifts);
  RUN_TEST(test_predicates);
  RUN_TEST(test_sync_event_ring);
  RUN_TEST(test_serialize_roundtrip);
  RUN_TEST(test_serialize_too_small_buffer);
  RUN_TEST(test_deserialize_bad_version);
  RUN_TEST(test_deserialize_clamps_count);
  RUN_TEST(test_deserialize_truncated_records);
  RUN_TEST(test_name_length_clamping);
  return UNITY_END();
}
