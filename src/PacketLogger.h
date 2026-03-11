#pragma once

#include "Logger.h"

namespace mesh {

/// @brief The global logging instance meant for packet logging
///
/// By default this is set to debugLog, but can be overriden by defining
/// CUSTOM_PACKET_LOG in a build configuration and then providing an
/// alternative global definition (eg. `Logger& packetLog = myCustomLogger;`)
extern Logger& packetLog;

}  // namespace mesh
