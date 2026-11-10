#include "TrackerMesh.h"

TrackerMesh::TrackerMesh(mesh::Radio& radio, mesh::RNG& rng,
                         mesh::RTCClock& rtc, mesh::MeshTables& tables)
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(32), tables)
{
    _fs             = NULL;
    _channel_loaded = false;
    _emergency      = false;
    _next_broadcast = 0;

    memset(&_prefs, 0, sizeof(_prefs));
    memset(&_channel, 0, sizeof(_channel));

    // defaults
    strncpy(_prefs.callsign, ADVERT_NAME, sizeof(_prefs.callsign) - 1);
    _prefs.callsign[sizeof(_prefs.callsign) - 1] = 0;
    _prefs.interval_secs = TRACKER_DEFAULT_INTERVAL_SECS;
    _prefs.role          = KESTREL_ROLE_TEAM_MEMBER;
    _prefs.channel_idx   = 0;
}

void TrackerMesh::begin(FILESYSTEM* fs) {
    mesh::Mesh::begin();
    _fs = fs;

    loadPrefs();
    loadChannel();

    radio_set_params(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR);
    radio_set_tx_power(LORA_TX_POWER);

    // stagger first broadcast by 5s to allow radio to settle
    _next_broadcast = futureMillis(5000);
}

/* ------------------------------------------------------------------ */
/* Persistence                                                          */
/* ------------------------------------------------------------------ */

void TrackerMesh::loadPrefs() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    File f = _fs->open(TRACKER_PREFS_FILENAME, FILE_O_READ);
#else
    File f = _fs->open(TRACKER_PREFS_FILENAME, "r");
#endif
    if (f) {
        f.read((uint8_t*)&_prefs, sizeof(_prefs));
        f.close();
    }
}

void TrackerMesh::savePrefs() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    File f = _fs->open(TRACKER_PREFS_FILENAME, FILE_O_WRITE);
#else
    File f = _fs->open(TRACKER_PREFS_FILENAME, "w");
#endif
    if (f) {
        f.write((uint8_t*)&_prefs, sizeof(_prefs));
        f.close();
    }
}

void TrackerMesh::loadChannel() {
    ChannelDetails ch;
    if (getChannel(_prefs.channel_idx, ch)) {
        _channel       = ch;
        _channel_loaded = true;
    } else {
        _channel_loaded = false;
    }
}

/* ------------------------------------------------------------------ */
/* PLI broadcast                                                        */
/* ------------------------------------------------------------------ */

uint8_t TrackerMesh::batteryPct() {
    uint16_t mv = board.getBattMilliVolts();
    // LiPo: ~4200mV full, ~3300mV empty
    if (mv >= 4200) return 100;
    if (mv <= 3300) return 0;
    return (uint8_t)(((uint32_t)(mv - 3300) * 100) / 900);
}

void TrackerMesh::broadcastPLI() {
    if (!_channel_loaded) {
        // try again in case a channel was added after begin()
        loadChannel();
        if (!_channel_loaded) return;
    }

    KestrelPLIPacket pli;
    memset(&pli, 0, sizeof(pli));

    pli.data_type   = KESTREL_PLI_DATA_TYPE_U16;
    pli.battery_pct = batteryPct();
    pli.role        = _prefs.role;

    // callsign
    uint8_t cs_len = (uint8_t)strlen(_prefs.callsign);
    if (cs_len > 16) cs_len = 16;
    pli.callsign_len = cs_len;
    memcpy(pli.callsign, _prefs.callsign, cs_len);

    // GPS — pull from SensorManager if available
    LocationProvider* gps = sensors.getLocationProvider();
    if (gps != NULL && gps->isValid()) {
        pli.lat_e7     = (int32_t)gps->getLatitude();   // returns lat * 1e7
        pli.lon_e7     = (int32_t)gps->getLongitude();  // returns lon * 1e7
        pli.alt_m      = (int16_t)(gps->getAltitude() / 1000);  // mm → m
        pli.fix_quality = (gps->satellitesCount() >= 4) ? KESTREL_FIX_3D : KESTREL_FIX_2D;
    } else {
        pli.fix_quality = KESTREL_FIX_NONE;
    }

    // also update SensorManager coords so Advert location is current
    if (pli.fix_quality != KESTREL_FIX_NONE) {
        sensors.node_lat = (double)pli.lat_e7 / 1e7;
        sensors.node_lon = (double)pli.lon_e7 / 1e7;
        sensors.node_altitude = (double)pli.alt_m;
    }

    sendGroupData(_channel.channel, NULL, OUT_PATH_UNKNOWN,
                  KESTREL_PLI_DATA_TYPE_U16,
                  (const uint8_t*)&pli, (int)KESTREL_PLI_PACKET_SIZE);

    // LED heartbeat — use TX LED if defined by the variant
#ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, HIGH);
    delay(50);
    digitalWrite(P_LORA_TX_LED, LOW);
#elif defined(PIN_STATUS_LED)
    digitalWrite(PIN_STATUS_LED, HIGH);
    delay(50);
    digitalWrite(PIN_STATUS_LED, LOW);
#endif
}

/* ------------------------------------------------------------------ */
/* CLI commands                                                         */
/* ------------------------------------------------------------------ */

void TrackerMesh::handleCommand(char* command, char* reply) {
    // skip leading spaces
    while (*command == ' ') command++;

    if (strncmp(command, "set_callsign ", 13) == 0) {
        char* name = command + 13;
        int len = strlen(name);
        if (len == 0 || len > 16) {
            strcpy(reply, "Err - callsign must be 1-16 chars");
        } else {
            strncpy(_prefs.callsign, name, 16);
            _prefs.callsign[16] = 0;
            savePrefs();
            sprintf(reply, "Callsign set: %s", _prefs.callsign);
        }
    } else if (strncmp(command, "set_interval ", 13) == 0) {
        uint32_t secs = (uint32_t)atoi(command + 13);
        if (secs < TRACKER_MIN_INTERVAL_SECS || secs > TRACKER_MAX_INTERVAL_SECS) {
            sprintf(reply, "Err - interval must be %d-%d secs",
                    TRACKER_MIN_INTERVAL_SECS, TRACKER_MAX_INTERVAL_SECS);
        } else {
            _prefs.interval_secs = secs;
            savePrefs();
            sprintf(reply, "Interval set: %u secs", _prefs.interval_secs);
        }
    } else if (strncmp(command, "set_role ", 9) == 0) {
        uint8_t role = (uint8_t)atoi(command + 9);
        if (role > 3) {
            strcpy(reply, "Err - role must be 0-3 (0=member,1=lead,2=casevac,3=asset)");
        } else {
            _prefs.role = role;
            savePrefs();
            const char* role_names[] = { "member", "lead", "casevac", "asset" };
            sprintf(reply, "Role set: %s", role_names[role]);
        }
    } else if (strcmp(command, "emergency") == 0) {
        _emergency      = true;
        _next_broadcast = 0;  // broadcast immediately on next loop
        strcpy(reply, "EMERGENCY mode: 5s interval");
    } else if (strcmp(command, "normal") == 0) {
        _emergency      = false;
        _next_broadcast = futureMillis((unsigned long)_prefs.interval_secs * 1000);
        strcpy(reply, "Normal mode restored");
    } else if (strcmp(command, "status") == 0) {
        uint8_t fix = KESTREL_FIX_NONE;
        long lat = 0, lon = 0;
        LocationProvider* gps = sensors.getLocationProvider();
        if (gps != NULL && gps->isValid()) {
            fix = (gps->satellitesCount() >= 4) ? KESTREL_FIX_3D : KESTREL_FIX_2D;
            lat = gps->getLatitude();
            lon = gps->getLongitude();
        }
        const char* fix_str = (fix == KESTREL_FIX_3D) ? "3D" :
                              (fix == KESTREL_FIX_2D) ? "2D" : "none";
        sprintf(reply,
                "callsign=%s role=%u interval=%us batt=%u%% gps=%s lat=%ld lon=%ld mode=%s",
                _prefs.callsign,
                (unsigned)_prefs.role,
                (unsigned)_prefs.interval_secs,
                (unsigned)batteryPct(),
                fix_str,
                lat, lon,
                _emergency ? "EMERGENCY" : "normal");
    } else if (strcmp(command, "broadcast") == 0) {
        broadcastPLI();
        strcpy(reply, "PLI broadcast sent");
    } else {
        sprintf(reply, "Unknown command. Commands: set_callsign, set_interval, set_role, emergency, normal, status, broadcast");
    }
}

/* ------------------------------------------------------------------ */
/* Main loop                                                            */
/* ------------------------------------------------------------------ */

void TrackerMesh::loop() {
    BaseChatMesh::loop();

    if (millisHasNowPassed(_next_broadcast)) {
        broadcastPLI();
        uint32_t interval = _emergency ? TRACKER_EMERGENCY_INTERVAL_SECS : _prefs.interval_secs;
        _next_broadcast = futureMillis((unsigned long)interval * 1000);
    }
}
