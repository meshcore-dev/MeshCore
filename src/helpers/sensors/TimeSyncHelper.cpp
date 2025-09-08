#include "TimeSyncHelper.h"

#ifdef ENV_INCLUDE_GPS

char TimeSyncHelper::_nmeaBuffer[100];
MicroNMEA TimeSyncHelper::_nmea;
unsigned long TimeSyncHelper::_lastSyncTime = -48 * 60 * 60 * 1000;
unsigned long TimeSyncHelper::_rolloverCount = 0;
bool TimeSyncHelper::_gpsWasOnBeforeSync = false;
bool TimeSyncHelper::_initialized = false;
bool TimeSyncHelper::_initialSyncCompleted = false;

TimeSyncHelper::SyncState TimeSyncHelper::_syncState = SYNC_IDLE;
unsigned long TimeSyncHelper::_syncStartTime = 0;
int TimeSyncHelper::_lockFixCount = 0;
int TimeSyncHelper::_initialSyncTimeoutCounter = 0;

void TimeSyncHelper::init() {
    if (_initialized) return;
    
    _nmea = MicroNMEA(_nmeaBuffer, sizeof(_nmeaBuffer));
    _lastSyncTime = -48 * 60 * 60 * 1000;
    _rolloverCount = 0;
    _gpsWasOnBeforeSync = false;
    _syncState = SYNC_IDLE;
    _syncStartTime = 0;
    _lockFixCount = 0;
    _initialSyncTimeoutCounter = 0;
    _initialSyncCompleted = false;
    _initialized = true;
    
    MESH_DEBUG_PRINTLN("TimeSyncHelper initialized");
}

bool TimeSyncHelper::isGPSValid() {
    return _nmea.isValid() && _nmea.getNumSatellites() >= 3; // 3 sats needed for proper time
}

long TimeSyncHelper::getGPSTimestamp() {
    DateTime dt(_nmea.getYear(), _nmea.getMonth(), _nmea.getDay(), 
                _nmea.getHour(), _nmea.getMinute(), _nmea.getSecond());
    return dt.unixtime();
}

bool TimeSyncHelper::isInitialSyncCompleted() {
    return _initialSyncCompleted;
}

void TimeSyncHelper::process() {
    if (!_initialized) {
        init();
    }
    
    if (sensors.gps_active) {
        while (Serial1.available()) {
            char c = Serial1.read();
            #ifdef MESH_DEBUG
            Serial.print(c);
            #endif
            _nmea.process(c);
        }
    }

    if (!_initialSyncCompleted) {
        if (_initialSyncTimeoutCounter == 0) {
            _gpsWasOnBeforeSync = sensors.gps_active;
            if (!sensors.gps_active) {
                sensors.start_gps();
            }
            _lockFixCount = 0;
            MESH_DEBUG_PRINTLN("Starting initial GPS sync");
        }

        _initialSyncTimeoutCounter++;

        if (isGPSValid()) {
            _lockFixCount++;
            MESH_DEBUG_PRINTLN("GPS fix count: %d/%d", _lockFixCount, 3);
        } else {
            _lockFixCount = 0;
        }

        // A man with one watch knows what time it is; a man with two is never sure, so 3 it is
        if (_lockFixCount >= 3) { 
            MESH_DEBUG_PRINTLN("GPS lock achieved with %d consecutive fixes", _lockFixCount);
            
            if (isGPSValid()) {
                long gpsTime = getGPSTimestamp();
                if (gpsTime > 0) {
                    the_mesh.getRTCClock()->setCurrentTime(gpsTime);
                    MESH_DEBUG_PRINTLN("GPS Time sync: %ld", gpsTime);
                }
            }

            if (!_gpsWasOnBeforeSync) {
                sensors.stop_gps();
            }
            
            _initialSyncCompleted = true;
        } else if (_initialSyncTimeoutCounter >= 300) {
            MESH_DEBUG_PRINTLN("GPS lock timeout - only achieved %d fixes", _lockFixCount);
            if (!_gpsWasOnBeforeSync) {
                sensors.stop_gps();
            }
            _initialSyncCompleted = true;
        }
    } else {
        switch (_syncState) {
            case SYNC_IDLE:
                unsigned long currentTime = millis();

                if (currentTime < _lastSyncTime) {
                    _rolloverCount++;
                }

                if ((currentTime - _lastSyncTime) >= (48 * 60 * 60 * 1000) + (_rolloverCount * 4294967296UL)) {
                    _lastSyncTime = currentTime;
                    _rolloverCount = 0;

                    _gpsWasOnBeforeSync = sensors.gps_active;

                    if (!sensors.gps_active) {
                        sensors.start_gps();
                    }

                    if (!_gpsWasOnBeforeSync) {
                        _syncState = SYNC_WAITING_FOR_LOCK;
                        _syncStartTime = millis();
                        _lockFixCount = 0;
                        MESH_DEBUG_PRINTLN("Starting GPS sync - waiting for lock");
                    } else {
                        if (isGPSValid()) {
                            long gpsTime = getGPSTimestamp();
                            if (gpsTime > 0) {
                                the_mesh.getRTCClock()->setCurrentTime(gpsTime);
                                MESH_DEBUG_PRINTLN("GPS Time sync: %ld", gpsTime);
                            }
                        }
                        _syncState = SYNC_IDLE;
                    }
                }
                break;

            case SYNC_WAITING_FOR_LOCK:
                if (millis() - _syncStartTime > 30000) {
                    MESH_DEBUG_PRINTLN("GPS lock timeout - only achieved %d fixes", _lockFixCount);
                    if (!_gpsWasOnBeforeSync) {
                        sensors.stop_gps();
                    }
                    _syncState = SYNC_IDLE;
                    break;
                }

                if (isGPSValid()) {
                    _lockFixCount++;
                    MESH_DEBUG_PRINTLN("GPS fix count: %d/%d", _lockFixCount, 3);
                } else {
                    _lockFixCount = 0;
                }

                if (_lockFixCount >= 3) {
                    MESH_DEBUG_PRINTLN("GPS lock achieved with %d consecutive fixes", _lockFixCount);
                    
                    if (isGPSValid()) {
                        long gpsTime = getGPSTimestamp();
                        if (gpsTime > 0) {
                            the_mesh.getRTCClock()->setCurrentTime(gpsTime);
                            MESH_DEBUG_PRINTLN("GPS Time sync: %ld", gpsTime);
                        }
                    }

                    if (!_gpsWasOnBeforeSync) {
                        sensors.stop_gps();
                    }
                    
                    _syncState = SYNC_IDLE;
                }
                break;
        }
    }
}

#endif