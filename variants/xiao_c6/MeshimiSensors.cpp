#include "MeshimiSensors.h"
#include "target.h"
#include <math.h>

bool MeshimiSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  // Add battery temperature from MAX17261 when environment telemetry is requested
  if (requester_permissions & TELEM_PERM_ENVIRONMENT) {
    float t = board.getBattTemperatureC();
    if (!isnan(t)) {
      telemetry.addTemperature(TELEM_CHANNEL_SELF, t);
    }
  }
  return true;
}


