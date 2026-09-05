#pragma once

#include <stdint.h>
#include <string.h>  // strcmp/memcmp used by the inline preset helpers below

// Maximum number of configurable MQTT connection slots (available to all builds for struct layout).
// Used in NodePrefs/MQTTPrefs for persistent storage — do NOT change without migration.
static const int MAX_MQTT_SLOTS = 6;

// Runtime slot array size: fewer slots on non-PSRAM boards to save ~1.2KB of heap.
// Non-PSRAM boards are limited to 2 active connections (_max_active_slots), so 3 runtime
// slots (2 active + 1 spare for reconfiguration) is sufficient.
#if defined(BOARD_HAS_PSRAM)
static const int RUNTIME_MQTT_SLOTS = 6;
#else
static const int RUNTIME_MQTT_SLOTS = 3;
#endif

#ifdef WITH_MQTT_BRIDGE

enum MQTTAuthType : uint8_t {
  MQTT_AUTH_NONE,      // No authentication
  MQTT_AUTH_USERPASS,   // Username/password
  MQTT_AUTH_JWT         // Ed25519-signed JWT (device identity)
};

enum MQTTTopicStyle : uint8_t {
  MQTT_TOPIC_MESHCORE,   // meshcore/{iata}/{device_id}/{status|packets|raw}
  MQTT_TOPIC_MESHRANK,   // meshrank/uplink/{token}/{device_id}/{type} (no raw)
};

struct MQTTPresetDef {
  const char* name;           // Preset identifier: "analyzer-us", "analyzer-eu", "meshmapper", "meshrank", "waev", ...
  const char* server_url;     // Full URL including scheme: "wss://host:port/path" or "mqtts://host:port"
  const char* jwt_audience;   // JWT audience field (only for MQTT_AUTH_JWT, nullptr otherwise)
  const char* ca_cert;        // PEM CA certificate (nullptr to skip cert pinning)
  MQTTAuthType auth_type;
  MQTTTopicStyle topic_style;
  unsigned long token_lifetime; // JWT token lifetime in seconds (0 = use default 86400)
  bool allow_retain;            // Whether the broker allows the MQTT retain flag
  uint16_t keepalive;           // MQTT keepalive in seconds (0 = library default 120s)
  const char* userpass_username; // MQTT_AUTH_USERPASS: embedded username, or nullptr to use mqttN.username
  const char* userpass_password; // MQTT_AUTH_USERPASS: embedded password, or nullptr to use mqttN.password
};

// Sentinel: resolve MQTT username from device public-key hex at connect time.
// Braces match topic placeholders ({device}/{iata}); never send this string to the broker.
static const char MQTT_USERPASS_USERNAME_PUBKEY[] = "{pubkey}";

static inline bool mqttPresetUsesDevicePubkeyUsername(const MQTTPresetDef* preset) {
  return preset && preset->auth_type == MQTT_AUTH_USERPASS &&
         preset->userpass_username &&
         strcmp(preset->userpass_username, MQTT_USERPASS_USERNAME_PUBKEY) == 0;
}

// True when USERPASS username must come from mqttN.username (null embedded username).
// "{pubkey}" is an embedded sentinel, so it does not need a slot username.
static inline bool mqttPresetNeedsSlotUsername(const MQTTPresetDef* preset) {
  return preset && preset->auth_type == MQTT_AUTH_USERPASS &&
         !preset->userpass_username;
}

// True when USERPASS password must come from mqttN.password (null embedded password).
static inline bool mqttPresetNeedsSlotPassword(const MQTTPresetDef* preset) {
  return preset && preset->auth_type == MQTT_AUTH_USERPASS &&
         !preset->userpass_password;
}

// True when preset uses MQTT_AUTH_USERPASS but at least one credential comes from slot prefs.
static inline bool mqttPresetNeedsSlotCredentials(const MQTTPresetDef* preset) {
  return mqttPresetNeedsSlotUsername(preset) || mqttPresetNeedsSlotPassword(preset);
}

// Google Trust Services - GTS Root R4 (used by LetsMesh Analyzer)
static const char GTS_ROOT_R4[] PROGMEM =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDejCCAmKgAwIBAgIQf+UwvzMTQ77dghYQST2KGzANBgkqhkiG9w0BAQsFADBX\n"
    "MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n"
    "CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIzMTEx\n"
    "NTAzNDMyMVoXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n"
    "GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFI0\n"
    "MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE83Rzp2iLYK5DuDXFgTB7S0md+8Fhzube\n"
    "Rr1r1WEYNa5A3XP3iZEwWus87oV8okB2O6nGuEfYKueSkWpz6bFyOZ8pn6KY019e\n"
    "WIZlD6GEZQbR3IvJx3PIjGov5cSr0R2Ko4H/MIH8MA4GA1UdDwEB/wQEAwIBhjAd\n"
    "BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDwYDVR0TAQH/BAUwAwEB/zAd\n"
    "BgNVHQ4EFgQUgEzW63T/STaj1dj8tT7FavCUHYwwHwYDVR0jBBgwFoAUYHtmGkUN\n"
    "l8qJUC99BM00qP/8/UswNgYIKwYBBQUHAQEEKjAoMCYGCCsGAQUFBzAChhpodHRw\n"
    "Oi8vaS5wa2kuZ29vZy9nc3IxLmNydDAtBgNVHR8EJjAkMCKgIKAehhxodHRwOi8v\n"
    "Yy5wa2kuZ29vZy9yL2dzcjEuY3JsMBMGA1UdIAQMMAowCAYGZ4EMAQIBMA0GCSqG\n"
    "SIb3DQEBCwUAA4IBAQAYQrsPBtYDh5bjP2OBDwmkoWhIDDkic574y04tfzHpn+cJ\n"
    "odI2D4SseesQ6bDrarZ7C30ddLibZatoKiws3UL9xnELz4ct92vID24FfVbiI1hY\n"
    "+SW6FoVHkNeWIP0GCbaM4C6uVdF5dTUsMVs/ZbzNnIdCp5Gxmx5ejvEau8otR/Cs\n"
    "kGN+hr/W5GvT1tMBjgWKZ1i4//emhA1JG1BbPzoLJQvyEotc03lXjTaCzv8mEbep\n"
    "8RqZ7a2CPsgRbuvTPBwcOMBBmuFeU88+FSBX6+7iP0il8b4Z0QFqIwwMHfs/L6K1\n"
    "vepuoxtGzi4CZ68zJpiq1UvSqTbFJjtbD4seiMHl\n"
    "-----END CERTIFICATE-----\n";

// ISRG Root X1 (used by MeshMapper - Let's Encrypt root CA)
static const char ISRG_ROOT_X1[] PROGMEM =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

// Number of built-in presets
static const int MQTT_PRESET_COUNT = 35;

// Built-in preset definitions (stored in flash)
static const MQTTPresetDef MQTT_PRESETS[MQTT_PRESET_COUNT] = {
    //   name           url                                      audience                           rootCA         auth                topic                 tokenLife retain keepAlive user         pass
    { "analyzer-us",   "wss://mqtt-us-v1.letsmesh.net:443/mqtt",  "mqtt-us-v1.letsmesh.net",         GTS_ROOT_R4,   MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "analyzer-eu",   "wss://mqtt-eu-v1.letsmesh.net:443/mqtt",  "mqtt-eu-v1.letsmesh.net",         GTS_ROOT_R4,   MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "nz-analyzer",   "wss://meshcore-mqtt-1.baird.io:443",      "meshcore-mqtt-1.baird.io",        GTS_ROOT_R4,   MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "meshmapper",    "wss://mqtt.meshmapper.net:443/mqtt",       "mqtt.meshmapper.net",              ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "meshrank",      "mqtts://meshrank.net:8883",               nullptr,                           ISRG_ROOT_X1,  MQTT_AUTH_NONE,     MQTT_TOPIC_MESHRANK,  0,       false,  0,       nullptr,     nullptr     },
    // waev token_lifetime is 3300 (55 min) on purpose: the broker's real JWT TTL is
    // 60 min, and claiming less keeps fresh tokens accepted even with ~5 min of fast
    // device-clock skew (and off any exp-iat<=3600 boundary strictness). Do NOT
    // "fix" this to 3600 — the renewal race is handled separately by
    // tokenRenewalBufferSecs() in MQTTBridge, which renews another 5 min earlier.
    { "waev",          "wss://mqtt.waev.app:443/mqtt",            "mqtt.waev.app",                   GTS_ROOT_R4,   MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE, 3300,     false,  55,      nullptr,     nullptr     },
    { "meshomatic",    "wss://us-east.meshomatic.net:443/mqtt",   "us-east.meshomatic.net",          ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "cascadiamesh",  "wss://mqtt-v1.cascadiamesh.org:443/mqtt", "mqtt-v1.cascadiamesh.org",        ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "tennmesh",      "mqtt://mqtt.tennmesh.com:1883",           nullptr,                           nullptr,       MQTT_AUTH_USERPASS,  MQTT_TOPIC_MESHCORE,  0,       true,   55,      "mqttfeed",   "tc2live"   },
    { "nashmesh",      "mqtt://mqtt.nashme.sh:1883",              nullptr,                           nullptr,       MQTT_AUTH_USERPASS,  MQTT_TOPIC_MESHCORE,  0,       true,   55,      "meshdev",    "large4cats"},
    { "ctmesh",        "mqtt://mqtt.ctmesh.org:1883",             nullptr,                           nullptr,       MQTT_AUTH_USERPASS,  MQTT_TOPIC_MESHCORE,  0,       true,   60,      "meshdev",    "large4cats"},
    { "chimesh",       "wss://mqtt.chimesh.org:443",              "mqtt.chimesh.org",                ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "meshat.se",     "wss://meshcore-mqtt.meshat.se:443",       "meshcore-mqtt.meshat.se",         ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "eastidahomesh", "mqtt://live.eastidahomesh.com:1883",      nullptr,                           nullptr,       MQTT_AUTH_NONE,     MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "coloradomesh",  "wss://mqtt.meshcore.coloradomesh.org:443","mqtt.meshcore.coloradomesh.org", ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "dutchmeshcore-1", "wss://collector1.dutchmeshcore.nl:443/mqtt", "collector1.dutchmeshcore.nl",     GTS_ROOT_R4,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "dutchmeshcore-2", "wss://collector2.dutchmeshcore.nl:443/mqtt", "collector2.dutchmeshcore.nl",     GTS_ROOT_R4,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "meshcore-ca-1",   "wss://mqtt1.meshcore.ca:443/mqtt",          "mqtt1.meshcore.ca",               ISRG_ROOT_X1, MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "meshcore-ca-2",   "wss://mqtt2.meshcore.ca:443/mqtt",          "mqtt2.meshcore.ca",               ISRG_ROOT_X1, MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "meshcore-fi",     "wss://mc-mqtt.meshcore.fi:443/",            "mc-mqtt.meshcore.fi",             ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "okimesh-1",     "wss://mqtt1.okimesh.org:9002/mqtt",       "mqtt1.okimesh.org",               ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "okimesh-2",     "wss://mqtt2.okimesh.org:9002/mqtt",       "mqtt2.okimesh.org",               ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "inwmesh",         "mqtts://scope.inwmesh.org:8883",            nullptr,                           ISRG_ROOT_X1,  MQTT_AUTH_USERPASS,  MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "bostonmesh",      "wss://mqttmc01.bostonme.sh:443/mqtt",       "mqttmc01.bostonme.sh",            GTS_ROOT_R4,   MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "rflab",           "wss://mqtt.rflab.io:443",                   "mqtt.rflab.io",                   ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "ipnt.uk",         "wss://mqtt.ipnt.uk:443",                    "mqtt.ipnt.uk",                    ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "flmesh",       "wss://mcmqtt.jntconnections.com:443",       "mcmqtt.jntconnections.com",       GTS_ROOT_R4,   MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "corecomms",     "wss://mqtt.corecomms.net:443/mqtt",       "mqtt.corecomms.net",              GTS_ROOT_R4,   MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "meshtexas",     "wss://mqtt.meshtexas.org:443/mqtt",       "mqtt.meshtexas.org",              GTS_ROOT_R4,   MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    // Username is device pubkey hex at connect; password from mqttN.password. No TLS.
    { "mesh-chaun14",  "mqtt://mqtt.mesh.chaun14.fr:1884",        nullptr,                           nullptr,       MQTT_AUTH_USERPASS,  MQTT_TOPIC_MESHCORE,  0,       true,   60,      MQTT_USERPASS_USERNAME_PUBKEY, nullptr },
    // LetsMesh-compatible JWT; TLS is Let's Encrypt (ISRG Root X1), not GTS.
    { "wcmesh",        "wss://mqtt.wcmesh.com:443",               "mqtt.wcmesh.com",                 ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "atvirastinklas","wss://mqtt-mc.atvirastinklas.lt:443",      "mqtt-mc.atvirastinklas.lt",       GTS_ROOT_R4,   MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    // JWT token auth; LE Gen-Y ECDSA chain (YE2 → Root YE → X2) still anchors at ISRG Root X1.
    { "gomesh",        "wss://mqtt.gomesh.dev:443",               "mqtt.gomesh.dev",                 ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "idahomesh",     "wss://mqtt.idahomesh.org:443/mqtt",       "mqtt.idahomesh.org",              ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
    { "ntxmesh",       "wss://ntxmesh.dhovin.me:8883",            "ntxmesh.dhovin.me",               ISRG_ROOT_X1,  MQTT_AUTH_JWT,      MQTT_TOPIC_MESHCORE,  0,       true,   55,      nullptr,     nullptr     },
};

// Find a preset by name, returns nullptr if not found
static const MQTTPresetDef* findMQTTPreset(const char* name) {
  if (!name || name[0] == '\0') return nullptr;
  for (int i = 0; i < MQTT_PRESET_COUNT; i++) {
    if (strcmp(name, MQTT_PRESETS[i].name) == 0) {
      return &MQTT_PRESETS[i];
    }
  }
  return nullptr;
}

// Slot preset name constants
static const char MQTT_PRESET_NONE[] = "none";
static const char MQTT_PRESET_CUSTOM[] = "custom";

#endif
