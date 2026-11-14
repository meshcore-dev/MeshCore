#pragma once

#include <MeshCore.h>

// Shared by the chat/serial interfaces and their delivery adapters. Keep these
// limits here so callers can size buffers without including Arduino interfaces.
#define MAX_FRAME_SIZE  176   // +4 for transport codes (region scoping)
#define MAX_TEXT_LEN    (10*CIPHER_BLOCK_SIZE)  // must be LESS than (MAX_PACKET_PAYLOAD - 4 - CIPHER_MAC_SIZE - 1)
