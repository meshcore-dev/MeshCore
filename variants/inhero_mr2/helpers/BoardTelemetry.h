/*
 * Copyright (c) 2026 Inhero GmbH
 * SPDX-License-Identifier: MIT
 */
#pragma once

class BoardConfigContainer;
class CayenneLPP;

namespace inhero {

// Appends the MR2 battery and solar values after the existing sensor channels.
bool appendBoardTelemetry(BoardConfigContainer& cfg, CayenneLPP& telemetry);

} // namespace inhero
