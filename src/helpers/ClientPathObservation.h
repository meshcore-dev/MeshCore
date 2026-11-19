#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace mesh {

inline size_t observedClientPathByteLength(uint8_t encoded_path_len) {
  const size_t hash_count = encoded_path_len & 63U;
  const size_t hash_size = (encoded_path_len >> 6) + 1U;
  return hash_count * hash_size;
}

inline bool isValidObservedClientPathLength(uint8_t encoded_path_len,
                                            size_t capacity) {
  const size_t hash_size = (encoded_path_len >> 6) + 1U;
  return hash_size != 4U
      && observedClientPathByteLength(encoded_path_len) <= capacity;
}

template <typename Client>
void clearObservedClientPath(Client& client, uint8_t unknown_path) {
  memset(client.observed_path, 0, sizeof(client.observed_path));
  client.observed_path_len = unknown_path;
  client.observed_path_pending = false;
  client.observed_path_expiry = 0;
}

template <typename Client>
void beginObservedClientPath(Client& client, uint8_t unknown_path,
                             uint32_t expiry) {
  clearObservedClientPath(client, unknown_path);
  client.observed_path_pending = true;
  client.observed_path_expiry = expiry;
}

template <typename Client>
bool isObservedClientPathPending(Client& client, uint32_t now) {
  if (!client.observed_path_pending) return false;
  if (static_cast<int32_t>(now - client.observed_path_expiry) < 0) return true;

  client.observed_path_pending = false;
  client.observed_path_expiry = 0;
  return false;
}

// Keep the most recently received, authenticated PAYLOAD_TYPE_PATH separate
// from the selected output route so CLI reads never need to mutate out_path.
template <typename Client>
bool captureObservedClientPath(Client& client, const uint8_t* path,
                               uint8_t encoded_path_len, uint32_t now) {
  if (!isObservedClientPathPending(client, now)) return false;

  const size_t path_bytes = observedClientPathByteLength(encoded_path_len);
  if (!isValidObservedClientPathLength(
          encoded_path_len, sizeof(client.observed_path))
      || (path_bytes != 0 && path == NULL)) {
    return false;
  }

  memset(client.observed_path, 0, sizeof(client.observed_path));
  if (path_bytes != 0) {
    memcpy(client.observed_path, path, path_bytes);
  }
  client.observed_path_len = encoded_path_len;
  client.observed_path_pending = false;
  client.observed_path_expiry = 0;
  return true;
}

// Promote a previously observed PATH to the selected output route. Invalid
// sentinels (including UNKNOWN/FORCE_FLOOD) cannot be promoted.
template <typename Client>
bool promoteObservedClientPath(Client& client) {
  const uint8_t encoded_path_len = client.observed_path_len;
  if (!isValidObservedClientPathLength(
          encoded_path_len, sizeof(client.observed_path))) {
    return false;
  }

  const size_t path_bytes = observedClientPathByteLength(encoded_path_len);
  if (path_bytes > sizeof(client.out_path)) return false;

  memset(client.out_path, 0, sizeof(client.out_path));
  if (path_bytes != 0) {
    memcpy(client.out_path, client.observed_path, path_bytes);
  }
  client.out_path_len = encoded_path_len;
  return true;
}

} // namespace mesh
