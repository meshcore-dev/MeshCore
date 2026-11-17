#pragma once

#include <stddef.h>
#include <stdint.h>
#include <helpers/MessageLimits.h>

// Per-app cursor over ONE unchanged offline-queue entry. Text parts are generated
// during SYNC_NEXT_MESSAGE, never enqueued. No allocation, flash settings or UI
// dependency; the decoder workspace is shared by the single companion loop.
class MCOtxtDelivery {
public:
  static const size_t kFrameSize = MAX_FRAME_SIZE;
  static const size_t kTextSize = MAX_TEXT_LEN;
  void appStart(const uint8_t* app_info, size_t length);
  void resetMessage();  // also call when overflow evicts the queue head

  // output has MAX_FRAME_SIZE+1 bytes (including formatter's temporary NUL),
  // separate from source; source stays owned by the queue and is never modified.
  // consume tells the queue when the source may be removed. A zero return with
  // consume=true means binary was already sent and has no decoded text to add;
  // the caller may continue with the next queue entry in the same SYNC request.
  int next(const uint8_t* source, size_t length, uint8_t app_version, uint32_t now,
           uint8_t* output, bool& consume);

private:
  bool _supports_mctxt = false;
  bool _original_sent = false;
  size_t _part = 0;
  uint32_t _timestamp = 0;
};
