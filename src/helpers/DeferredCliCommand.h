#pragma once

#include <MeshCore.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace mesh {

// A single-entry mailbox for authenticated remote CLI commands. The repeater
// drains it immediately after Mesh::loop() returns; a second command arriving
// in the same pass receives a busy response. Keeping the command in object
// storage lets the receive/decrypt call chain unwind before command handlers
// perform filesystem or cryptographic work.
struct DeferredCliCommand {
  bool pending;
  int client_index;
  uint32_t sender_timestamp;
  uint8_t path_hash_size;
  uint8_t secret[PUB_KEY_SIZE];
  char command[MAX_PACKET_PAYLOAD + 1];

  DeferredCliCommand()
      : pending(false), client_index(-1), sender_timestamp(0), path_hash_size(1) {
    memset(secret, 0, sizeof(secret));
    command[0] = 0;
  }

  bool enqueue(int new_client_index, uint32_t new_sender_timestamp,
               uint8_t new_path_hash_size, const uint8_t* new_secret,
               const char* new_command, size_t command_len) {
    if (pending || new_secret == NULL || new_command == NULL
        || command_len >= sizeof(command)) {
      return false;
    }

    client_index = new_client_index;
    sender_timestamp = new_sender_timestamp;
    path_hash_size = new_path_hash_size;
    memcpy(secret, new_secret, sizeof(secret));
    memcpy(command, new_command, command_len);
    command[command_len] = 0;
    pending = true;
    return true;
  }

  void clear() {
    pending = false;
    client_index = -1;
    sender_timestamp = 0;
    path_hash_size = 1;
    memset(secret, 0, sizeof(secret));
    memset(command, 0, sizeof(command));
  }
};

} // namespace mesh
