#pragma once

#ifdef __cplusplus
  // Include the device headers first so subsequent Arduino includes cannot
  // reintroduce the peripheral macro. It conflicts with both mesh::RNG and
  // Crypto's global RNG object. Leave C HAL sources' register macros intact.
  #include <Arduino.h>
  #ifdef RNG
    #undef RNG
  #endif
#endif
