#include "PacketLogger.h"

#ifndef CUSTOM_PACKET_LOG
#include <DebugLogger.h>

namespace mesh {

// The default logger for packet logging is just the debug logger.
Logger& packetLog = debugLog;

}  // namespace mesh

#endif
