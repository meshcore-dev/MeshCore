#include "ChatStore.h"
#include <helpers/TxtDataHelpers.h>

#ifdef ESP32
  #include <SPIFFS.h>
#endif

#define CHAT_HIST_FILE   "/chat_hist.bin"
#define CHAT_HIST_MAGIC  0x43483032   // "CH02"

static ChatMsg chat_store[CHAT_STORE_SIZE];
static int chat_head = 0;
static bool chat_dirty = false;
static unsigned long chat_flush_at = 0;

void makeChannelKey(int idx, uint8_t key[6]) {
  key[0] = 0xFE; key[1] = 0xC4; key[2] = (uint8_t)idx;
  key[3] = 0x00; key[4] = 0xBE; key[5] = 0xEF;
}

bool isChannelKey(const uint8_t key[6], int* idx_out) {
  if (key[0] == 0xFE && key[1] == 0xC4 && key[3] == 0x00 && key[4] == 0xBE && key[5] == 0xEF) {
    if (idx_out) *idx_out = key[2];
    return true;
  }
  return false;
}

void chatStoreMarkDirty() {
  chat_dirty = true;
  chat_flush_at = millis() + 4000;
}

ChatMsg* chatStorePush(const uint8_t* pub_key, bool outgoing, uint32_t timestamp, const char* text) {
  auto m = &chat_store[chat_head];
  chat_head = (chat_head + 1) % CHAT_STORE_SIZE;
  memcpy(m->prefix, pub_key, 6);
  m->outgoing = outgoing ? 1 : 0;
  m->valid = 1;
  m->status = MSG_STATUS_NONE;
  m->expected_ack = 0;
  m->timeout_at = 0;
  m->timestamp = timestamp;
  StrHelper::strncpy(m->text, text, sizeof(m->text));
  chatStoreMarkDirty();
  return m;
}

ChatMsg* chatStoreGet(const uint8_t* pub_key, int k) {
  int found = 0;
  for (int i = 0; i < CHAT_STORE_SIZE; i++) {
    int idx = (chat_head - 1 - i + CHAT_STORE_SIZE * 2) % CHAT_STORE_SIZE;
    auto m = &chat_store[idx];
    if (!m->valid || memcmp(m->prefix, pub_key, 6) != 0) continue;
    if (found == k) return m;
    found++;
  }
  return NULL;
}

bool chatStoreAck(uint32_t ack_crc) {
  bool changed = false;
  for (int i = 0; i < CHAT_STORE_SIZE; i++) {
    auto m = &chat_store[i];
    if (m->valid && m->status == MSG_STATUS_PENDING && m->expected_ack == ack_crc) {
      m->status = MSG_STATUS_DELIVERED;
      m->expected_ack = 0;
      changed = true;
    }
  }
  if (changed) chatStoreMarkDirty();
  return changed;
}

int chatStoreThreads(uint8_t keys[][6], int max) {
  int count = 0;
  for (int i = 0; i < CHAT_STORE_SIZE && count < max; i++) {
    int idx = (chat_head - 1 - i + CHAT_STORE_SIZE * 2) % CHAT_STORE_SIZE;
    auto m = &chat_store[idx];
    if (!m->valid) continue;
    bool seen = false;
    for (int j = 0; j < count; j++) {
      if (memcmp(keys[j], m->prefix, 6) == 0) { seen = true; break; }
    }
    if (!seen) memcpy(keys[count++], m->prefix, 6);
  }
  return count;
}

void chatStoreLoad() {
#ifdef ESP32
  if (!SPIFFS.begin(true)) return;
  File f = SPIFFS.open(CHAT_HIST_FILE, "r");
  if (!f) return;
  uint32_t magic = 0;
  int32_t head = 0;
  bool ok = f.read((uint8_t *)&magic, 4) == 4 && magic == CHAT_HIST_MAGIC
         && f.read((uint8_t *)&head, 4) == 4
         && f.read((uint8_t *)chat_store, sizeof(chat_store)) == sizeof(chat_store)
         && head >= 0 && head < CHAT_STORE_SIZE;
  f.close();
  if (ok) {
    chat_head = head;
    for (int i = 0; i < CHAT_STORE_SIZE; i++) {
      chat_store[i].text[sizeof(chat_store[i].text) - 1] = 0;   // guard vs flash corruption
      if (chat_store[i].valid && chat_store[i].status == MSG_STATUS_PENDING) {
        chat_store[i].status = MSG_STATUS_NO_ACK;   // acks can't match across reboot
      }
    }
  } else {
    memset(chat_store, 0, sizeof(chat_store));
    chat_head = 0;
  }
#endif
}

static void chatStoreSave() {
  chat_dirty = false;
#ifdef ESP32
  File f = SPIFFS.open(CHAT_HIST_FILE, "w");
  if (!f) return;
  uint32_t magic = CHAT_HIST_MAGIC;
  int32_t head = chat_head;
  f.write((const uint8_t *)&magic, 4);
  f.write((const uint8_t *)&head, 4);
  f.write((const uint8_t *)chat_store, sizeof(chat_store));
  f.close();
#endif
}

void chatStoreFlushLoop() {
  if (chat_dirty && millis() > chat_flush_at) {
    chatStoreSave();
  }
}

void chatStoreClearThread(const uint8_t key[6]) {
  for (int i = 0; i < CHAT_STORE_SIZE; i++) {
    if (chat_store[i].valid && memcmp(chat_store[i].prefix, key, 6) == 0) {
      chat_store[i].valid = 0;
    }
  }
  chatStoreMarkDirty();
}

// --- unread counters ---
#define UNREAD_SLOTS 24
struct UnreadSlot { uint8_t key[6]; int count; };
static UnreadSlot unread[UNREAD_SLOTS];

void chatStoreUnreadBump(const uint8_t key[6]) {
  int free_slot = -1;
  for (int i = 0; i < UNREAD_SLOTS; i++) {
    if (unread[i].count > 0 && memcmp(unread[i].key, key, 6) == 0) {
      unread[i].count++;
      return;
    }
    if (unread[i].count == 0 && free_slot < 0) free_slot = i;
  }
  if (free_slot >= 0) {
    memcpy(unread[free_slot].key, key, 6);
    unread[free_slot].count = 1;
  }
}

void chatStoreUnreadClear(const uint8_t key[6]) {
  for (int i = 0; i < UNREAD_SLOTS; i++) {
    if (unread[i].count > 0 && memcmp(unread[i].key, key, 6) == 0) unread[i].count = 0;
  }
}

int chatStoreUnreadGet(const uint8_t key[6]) {
  for (int i = 0; i < UNREAD_SLOTS; i++) {
    if (unread[i].count > 0 && memcmp(unread[i].key, key, 6) == 0) return unread[i].count;
  }
  return 0;
}

int chatStoreUnreadTotal() {
  int total = 0;
  for (int i = 0; i < UNREAD_SLOTS; i++) total += unread[i].count;
  return total;
}
