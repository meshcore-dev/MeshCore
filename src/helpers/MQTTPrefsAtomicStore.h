#pragma once

#include <stddef.h>
#include <stdint.h>

// Transactional writer for /mqtt_prefs. The Store interface is intentionally
// narrow so host tests can exercise every failure boundary without an Arduino
// filesystem: begin(), write(), finish(), commit(), and abort(). The caller
// supplies header and payload separately, avoiding a second full-size buffer.
namespace MQTTPrefsAtomicStore {

enum class Result : uint8_t {
  Committed,
  BeginFailed,
  HeaderWriteFailed,
  PayloadWriteFailed,
  FinishFailed,
  CommitFailed,
};

inline bool committed(Result result) {
  return result == Result::Committed;
}

// Generic streaming transaction for structured images such as /com_prefs.
// ImageWriter writes its fields directly to Store and returns false on any
// short write, so no contiguous staging allocation is required.
enum class ImageResult : uint8_t {
  Committed,
  BeginFailed,
  WriteFailed,
  FinishFailed,
  CommitFailed,
};

inline bool imageCommitted(ImageResult result) {
  return result == ImageResult::Committed;
}

template <typename Store, typename ImageWriter>
inline ImageResult writeImage(Store& store, ImageWriter write_image) {
  if (!store.begin()) {
    store.abort();
    return ImageResult::BeginFailed;
  }
  if (!write_image(store)) {
    store.abort();
    return ImageResult::WriteFailed;
  }
  if (!store.finish()) {
    store.abort();
    return ImageResult::FinishFailed;
  }
  if (!store.commit()) {
    store.abort();
    return ImageResult::CommitFailed;
  }
  return ImageResult::Committed;
}

// Coordinates a two-file legacy upgrade. /com_prefs must not be compacted
// until the observer tail it carries has been published into /mqtt_prefs.
// Keeping this state in a tiny pure helper lets host tests cover power-cut
// boundaries without an Arduino filesystem.
class LegacyUpgradeGate {
public:
  explicit LegacyUpgradeGate(bool com_prefs_rewrite_pending)
      : _com_prefs_rewrite_pending(com_prefs_rewrite_pending) {}

  void requireMqttRewrite() { _mqtt_rewrite_pending = true; }

  void recordMqttSave(bool did_commit) {
    if (did_commit) {
      _mqtt_rewrite_pending = false;
      _mqtt_source_held = false;
    } else {
      _mqtt_source_held = true;
    }
  }

  void holdMqttSource() { _mqtt_source_held = true; }

  bool mqttRewritePending() const { return _mqtt_rewrite_pending; }
  bool blocksComPrefsRewrite() const {
    return _com_prefs_rewrite_pending && (_mqtt_rewrite_pending || _mqtt_source_held);
  }
  bool mayRewriteComPrefs() const {
    return _com_prefs_rewrite_pending && !blocksComPrefsRewrite();
  }

  void recordComPrefsRewrite() {
    if (mayRewriteComPrefs()) _com_prefs_rewrite_pending = false;
  }

private:
  bool _com_prefs_rewrite_pending;
  bool _mqtt_rewrite_pending = false;
  bool _mqtt_source_held = false;
};

template <typename Store>
inline Result write(Store& store, const uint8_t* header, size_t header_size,
                    const uint8_t* payload, size_t payload_size) {
  if (!store.begin()) {
    store.abort();
    return Result::BeginFailed;
  }
  if (store.write(header, header_size) != header_size) {
    store.abort();
    return Result::HeaderWriteFailed;
  }
  if (store.write(payload, payload_size) != payload_size) {
    store.abort();
    return Result::PayloadWriteFailed;
  }
  if (!store.finish()) {
    store.abort();
    return Result::FinishFailed;
  }
  if (!store.commit()) {
    store.abort();
    return Result::CommitFailed;
  }
  return Result::Committed;
}

}  // namespace MQTTPrefsAtomicStore
