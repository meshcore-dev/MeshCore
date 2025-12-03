#pragma once

#include <stdint.h>
#include <stddef.h>

// -----------------------------------------------------------------------------
// Open Circuit Voltage (OCV) map configuration
//
// OCV_ARRAY must expand to 11 integer millivolt values, corresponding to:
//
//   100%, 90%, 80%, 70%, 60%, 50%, 40%, 30%, 20%, 10%, 0%
//
// in *descending* voltage order.
//
// -----------------------------------------------------------------------------

#ifndef OCV_ARRAY
  #ifdef CELL_TYPE_LIFEPO4
    #define OCV_ARRAY 3400, 3350, 3320, 3290, 3270, 3260, 3250, 3230, 3200, 3120, 3000
  #elif defined(CELL_TYPE_LEADACID)
    #define OCV_ARRAY 2120, 2090, 2070, 2050, 2030, 2010, 1990, 1980, 1970, 1960, 1950
  #elif defined(CELL_TYPE_ALKALINE)
    #define OCV_ARRAY 1580, 1400, 1350, 1300, 1280, 1250, 1230, 1190, 1150, 1100, 1000
  #elif defined(CELL_TYPE_NIMH)
    #define OCV_ARRAY 1400, 1300, 1280, 1270, 1260, 1250, 1240, 1230, 1210, 1150, 1000
  #elif defined(CELL_TYPE_LTO)
    #define OCV_ARRAY 2700, 2560, 2540, 2520, 2500, 2460, 2420, 2400, 2380, 2320, 1500
  #else 
    // Default Li-Ion / Li-Po
    #define OCV_ARRAY 4190, 4050, 3990, 3890, 3800, 3720, 3630, 3530, 3420, 3300, 3100
  #endif
#endif


/**
 * Convert a battery voltage (in millivolts) to approximate state-of-charge (%),
 * using the OCV curve defined by OCV_ARRAY.
 *
 * Assumes:
 *  - OCV_ARRAY has 11 entries, in descending order.
 *  - These correspond to 100, 90, 80, ..., 0 percent.
 *  - The input batteryMilliVolts is in the same scale as the OCV values.
 *
 * Returns an integer percentage in [0, 100] or -1 on error.
 */
int batteryPercentFromMilliVolts(uint16_t batteryMilliVolts);
