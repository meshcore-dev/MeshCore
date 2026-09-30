#include "BridgeLogger.h"

#ifndef CUSTOM_BRIDGE_LOG
#include "DebugLogger.h"

namespace mesh {

// The default logger for bridge logging is just the debug logger.
Logger& bridgeLog = debugLog;

} // namespace mesh

#endif
