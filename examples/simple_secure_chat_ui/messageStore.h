#pragma once
#include <stdint.h>

// Append-only per-contact / public chat persistence on SPIFFS.
// Each DM contact gets a file keyed by the first 4 bytes of the public key.
// When a file exceeds MSGSTORE_MAX_BYTES it is dropped (oldest history lost).

#define MSGSTORE_MAX_BYTES 16384

void msgstore_append_dm(const uint8_t* pub_key, uint32_t ts, bool sent, const char* text);
void msgstore_append_public(uint32_t ts, const char* sender, bool sent, const char* text);

// Replays stored history into the UI (calls uiManager->addPrivateChatBubble / addChatBubble).
void msgstore_load_dm(const uint8_t* pub_key);
void msgstore_load_public();
