#ifndef TIMESYNCHELPER_H
#define TIMESYNCHELPER_H

#include <Arduino.h>

#ifdef ENV_INCLUDE_GPS
#include <MicroNMEA.h>
#include <RTClib.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

class MyMesh;
extern MyMesh the_mesh;
extern EnvironmentSensorManager sensors;

class TimeSyncHelper {
public:
    static void init();
    
    static void process();
    
    static bool isInitialSyncCompleted();

private:
    static char _nmeaBuffer[100];
    static MicroNMEA _nmea;
    static unsigned long _lastSyncTime;
    static unsigned long _rolloverCount;
    static bool _gpsWasOnBeforeSync;
    static bool _initialized;
    static bool _initialSyncCompleted;
    
    static enum SyncState {
        SYNC_IDLE,
        SYNC_WAITING_FOR_LOCK
    } _syncState;
    static unsigned long _syncStartTime;
    static int _lockFixCount;
    static int _initialSyncTimeoutCounter;
    
    static bool isGPSValid();
    static long getGPSTimestamp();
};

#endif
