#pragma once

#include "Logger.h"

namespace mesh {

/// @brief The global logging instance meant for logs from bridge implementations
///
/// By default this is set to debugLog, but can be overriden by defining
/// CUSTOM_BRIDGE_LOG in a build configuration and then providing an
/// alternative global definition (eg. `Logger& bridgeLog = myCustomLogger;`)
extern Logger& bridgeLog;

}  // namespace mesh
