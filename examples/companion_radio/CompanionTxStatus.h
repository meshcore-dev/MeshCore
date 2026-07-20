#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace companion {

constexpr uint8_t TX_STATUS_PENDING = 0xFF;

inline bool isPendingTxMatch(uint32_t ack, uint8_t tx_status, const uint8_t* stored_hash,
                             const uint8_t* packet_hash, size_t hash_len) {
  return ack != 0 && tx_status == TX_STATUS_PENDING &&
         memcmp(stored_hash, packet_hash, hash_len) == 0;
}

inline bool shouldPushTxStatus(uint8_t app_target_ver) {
  return app_target_ver >= 14;
}

}  // namespace companion
