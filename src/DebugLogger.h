#pragma once

#include "Logger.h"

namespace mesh {

/// @brief The global logging instance meant for debug logs
///
/// By default this is set to an implementation which forwards to the
/// equivalent methods in the Arduino core `Serial` instance. This can be
/// overridden by defining CUSTOM_DEBUG_LOG in a build configuration and then
/// providing an alternative global definition. For example:
/// `Logger& debugLog = myCustomLogger;`.
extern Logger& debugLog;

}  // namespace mesh
