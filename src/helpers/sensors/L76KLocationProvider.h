#pragma once

#include "MicroNMEALocationProvider.h"

class L76KLocationProvider : public MicroNMEALocationProvider {
public:
    using MicroNMEALocationProvider::MicroNMEALocationProvider;

    void configure() override {
        MESH_DEBUG_PRINTLN("GPS: configure L76K");
        sendSentence("$PCAS04,7"); // GPS + GLONASS + BeiDou
        sendSentence("$PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0"); // RMC + GGA only
        sendSentence("$PCAS06,2"); // Query mode
    }
};
