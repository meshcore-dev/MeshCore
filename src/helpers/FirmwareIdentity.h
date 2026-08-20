#pragma once

#include <stdint.h>

namespace mesh {

// Build identity strings and OTA target id live in FirmwareIdentity.generated.cpp so release
// stamps do not invalidate every translation unit via global -D flags.
const char* firmware_version_string();
const char* firmware_build_date_string();
uint32_t firmware_mota_target_id();

// Semver packed for OTA serve-self when EndF fw_version is unset.
uint32_t firmware_version_packed();

}  // namespace mesh
