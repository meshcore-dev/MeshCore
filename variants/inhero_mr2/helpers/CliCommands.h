/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <Arduino.h>
#include <CayenneLPP.h>

class BoardConfigContainer;

namespace inhero {

// Handles `get board.<cmd>` queries. Writes formatted result into `reply`.
// Returns true if the command was recognised (always true currently — falls
// through to a usage hint on unknown commands).
bool handleGet(BoardConfigContainer& cfg, const char* cmd, char* reply, uint32_t maxlen);

// Handles `set board.<cmd> <value>` commands. Returns a pointer to a static
// reply buffer owned by the helper (caller must not free).
const char* handleSet(BoardConfigContainer& cfg, const char* setCommand);

// Appends battery + solar telemetry to `lpp` starting at the next free channel.
// Returns false if telemetry data is unavailable (cfg.getTelemetryData() == nullptr).
bool appendBoardTelemetry(BoardConfigContainer& cfg, CayenneLPP& lpp);

// Parses a CayenneLPP buffer and returns highest_used_channel + 1, or 1 if empty.
uint8_t findNextFreeLppChannel(CayenneLPP& lpp);

} // namespace inhero
