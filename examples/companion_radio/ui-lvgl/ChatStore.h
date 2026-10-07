#pragma once

#include <Arduino.h>

// On-device chat history, persisted to /chat_hist.bin so threads survive a
// reboot. Fixed-size ring: CHAT_STORE_SIZE messages across all threads.

#define MSG_STATUS_NONE      0
#define MSG_STATUS_PENDING   1
#define MSG_STATUS_DELIVERED 2
#define MSG_STATUS_NO_ACK    3

struct ChatMsg {
  uint8_t prefix[6];      // contact pub_key prefix; identifies the thread
  uint8_t outgoing;       // 1 = sent from this device
  uint8_t valid;
  uint8_t status;         // MSG_STATUS_*
  uint32_t timestamp;
  uint32_t expected_ack;  // ack CRC we're waiting for (pending only)
  uint32_t timeout_at;    // millis deadline for pending -> no_ack
  char text[120];
};

#ifndef CHAT_STORE_SIZE
  #define CHAT_STORE_SIZE 40
#endif

// synthetic 6-byte thread key for channel slot idx (vs contact pub_key prefix)
void makeChannelKey(int idx, uint8_t key[6]);
bool isChannelKey(const uint8_t key[6], int* idx_out);

ChatMsg* chatStorePush(const uint8_t* pub_key, bool outgoing, uint32_t timestamp, const char* text);
ChatMsg* chatStoreGet(const uint8_t* pub_key, int k);   // k-th newest in thread; NULL when exhausted
void chatStoreLoad();
void chatStoreFlushLoop();       // call from loop(); debounced save when dirty
void chatStoreMarkDirty();
bool chatStoreAck(uint32_t ack_crc);   // mark matching pending msg delivered

// distinct thread keys, newest activity first; returns count (<= max)
int chatStoreThreads(uint8_t keys[][6], int max);

void chatStoreClearThread(const uint8_t key[6]);   // delete a thread's messages

// per-thread unread counters (RAM only)
void chatStoreUnreadBump(const uint8_t key[6]);
void chatStoreUnreadClear(const uint8_t key[6]);
int  chatStoreUnreadGet(const uint8_t key[6]);
int  chatStoreUnreadTotal();
