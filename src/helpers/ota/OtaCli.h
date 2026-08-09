#pragma once

#include <MeshCore.h>

// Text-CLI surface for OTA (P3/P5). Wired from CommonCLI (and reachable over LoRa remote-admin).
// Kept out of CommonCLI.cpp itself so the OTA state (allowlist, staging store) lives in the OTA module.

namespace mesh {
namespace ota {

// Handle an "ota ..." command. `command` is the full line (starts with "ota"). Fills `reply`
// (<= ~160 bytes, as per the CLI buffer). Returns true if it was an OTA command.
bool handle_ota_command(const char* command, char* reply, mesh::MainBoard& board);

} // namespace ota
} // namespace mesh
