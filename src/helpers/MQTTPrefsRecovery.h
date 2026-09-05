#pragma once

#include <stdint.h>

// Pure recovery policy for the three MQTT preference transaction files.  The
// writer first moves the old primary to .bak, then moves the verified .tmp to
// the primary name.  On a reset, the loader uses this policy before decoding
// /mqtt_prefs.  "Preserve" is deliberately distinct from "Usable": it covers
// an unsupported newer layout, corruption, or an unreadable file and must
// never be replaced by an older image.
namespace MQTTPrefsRecovery {

enum class FileState : uint8_t {
  Missing,
  Usable,
  Preserve,
};

enum class Action : uint8_t {
  None,
  KeepPrimary,
  PromoteTemp,
  PromoteBackup,
};

inline Action select(FileState primary, FileState temp, FileState backup) {
  // A primary of any kind owns the name. In particular, do not roll a newer
  // or corrupt primary back to an older backup just because it cannot be read
  // by this firmware.
  if (primary != FileState::Missing) return Action::KeepPrimary;

  // A completed temp is the new image and wins over the old backup.
  if (temp == FileState::Usable) return Action::PromoteTemp;

  // If temp is opaque but a known-good backup exists, boot from the backup.
  // The caller may discard the opaque temp once that usable backup has become
  // primary. Otherwise, rename the opaque temp into the empty primary name so
  // the normal loader can hold it.
  if (temp == FileState::Preserve) {
    return backup == FileState::Usable ? Action::PromoteBackup : Action::PromoteTemp;
  }

  // No temp survived. The backup is the only recoverable image, even when it
  // is a newer layout that this firmware must preserve rather than decode.
  if (backup != FileState::Missing) return Action::PromoteBackup;
  return Action::None;
}

}  // namespace MQTTPrefsRecovery
