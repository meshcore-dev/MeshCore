#include "ProvisioningManager.h"

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

// LED pin — prefer PIN_STATUS_LED (T1000-E: GPIO 24 / LED_GREEN)
#ifndef PROV_LED_PIN
  #ifdef PIN_STATUS_LED
    #define PROV_LED_PIN  PIN_STATUS_LED
  #elif defined(LED_PIN)
    #define PROV_LED_PIN  LED_PIN
  #else
    #define PROV_LED_PIN  -1
  #endif
#endif

/* ------------------------------------------------------------------ */
/* LED helpers                                                          */
/* ------------------------------------------------------------------ */

void ProvisioningManager::ledOn() {
#if PROV_LED_PIN >= 0
    digitalWrite(PROV_LED_PIN, HIGH);
#endif
    _led_state = true;
}

void ProvisioningManager::ledOff() {
#if PROV_LED_PIN >= 0
    digitalWrite(PROV_LED_PIN, LOW);
#endif
    _led_state = false;
}

void ProvisioningManager::setLEDPattern(LEDPattern pat, unsigned long now_ms) {
    _led_pattern     = pat;
    _led_next_ms     = now_ms;
    _led_blink_count = 0;
    _led_state       = false;
    ledOff();
}

void ProvisioningManager::indicateLED(unsigned long now_ms) {
    if (_led_pattern == LED_PAT_NONE) return;
    if ((long)(now_ms - _led_next_ms) < 0) return;

    switch (_led_pattern) {

        case LED_PAT_SLOW_PULSE:
            // 500ms on / 500ms off, indefinite
            if (_led_state) {
                ledOff();
                _led_next_ms = now_ms + 500;
            } else {
                ledOn();
                _led_next_ms = now_ms + 500;
            }
            break;

        case LED_PAT_TRIPLE_BLINK: {
            // rapid 100ms triple blink, then stop (3 on + 3 off = 6 transitions)
            if (_led_blink_count >= 6) {
                _led_pattern = LED_PAT_NONE;
                ledOff();
                break;
            }
            if (_led_blink_count % 2 == 0) { ledOn(); } else { ledOff(); }
            _led_blink_count++;
            _led_next_ms = now_ms + 100;
            break;
        }

        case LED_PAT_TRACKER_OK: {
            // 3 long blinks (300ms on / 200ms off)
            if (_led_blink_count >= 6) {
                _led_pattern = LED_PAT_NONE;
                ledOff();
                break;
            }
            if (_led_blink_count % 2 == 0) { ledOn(); _led_next_ms = now_ms + 300; }
            else                           { ledOff(); _led_next_ms = now_ms + 200; }
            _led_blink_count++;
            break;
        }

        case LED_PAT_RELAY_TIMEOUT:
            // 1 long blink (1000ms on then off)
            if (_led_blink_count >= 2) {
                _led_pattern = LED_PAT_NONE;
                ledOff();
                break;
            }
            if (_led_blink_count == 0) { ledOn();  _led_next_ms = now_ms + 1000; }
            else                       { ledOff(); _led_next_ms = now_ms + 200; }
            _led_blink_count++;
            break;

        case LED_PAT_REPROVISION: {
            // 5 rapid blinks
            if (_led_blink_count >= 10) {
                _led_pattern = LED_PAT_NONE;
                ledOff();
                break;
            }
            if (_led_blink_count % 2 == 0) { ledOn(); } else { ledOff(); }
            _led_blink_count++;
            _led_next_ms = now_ms + 80;
            break;
        }

        default:
            _led_pattern = LED_PAT_NONE;
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Base64 decode — standard alphabet, no padding required              */
/* ------------------------------------------------------------------ */

static const int8_t B64_TAB[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 0x00
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 0x10
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  // 0x20  '+' '/'
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,  // 0x30  '0'-'9' '='
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  // 0x40  'A'-'O'
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  // 0x50  'P'-'Z'
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  // 0x60  'a'-'o'
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  // 0x70  'p'-'z'
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

/*static*/ int ProvisioningManager::base64Decode(const char* src, uint8_t* dst, size_t dst_max) {
    size_t out = 0;
    uint32_t acc = 0;
    int bits = 0;

    while (*src) {
        char c = *src++;
        if (c == '=') break;
        int8_t v = B64_TAB[(uint8_t)c];
        if (v < 0) continue;  // skip whitespace / unknown chars
        acc = (acc << 6) | (uint8_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out >= dst_max) return -1;  // overflow
            dst[out++] = (uint8_t)(acc >> bits);
            acc &= (1u << bits) - 1;
        }
    }
    return (int)out;
}

/* ------------------------------------------------------------------ */
/* JSON field extractor — no malloc, strstr + manual scan              */
/* ------------------------------------------------------------------ */

// Extract a string value for a given key from a flat JSON object.
// Writes up to (out_max-1) chars + null terminator.  Returns true on success.
static bool jsonGetString(const char* json, const char* key, char* out, size_t out_max) {
    // Build search token  "key":
    char token[32];
    snprintf(token, sizeof(token), "\"%s\"", key);
    const char* p = strstr(json, token);
    if (!p) return false;
    p += strlen(token);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return false;
    p++;  // skip opening quote
    size_t i = 0;
    while (*p && *p != '"' && i < out_max - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (i > 0);
}

// Extract an integer value for a given key from a flat JSON object.
static bool jsonGetInt(const char* json, const char* key, int* out) {
    char token[32];
    snprintf(token, sizeof(token), "\"%s\"", key);
    const char* p = strstr(json, token);
    if (!p) return false;
    p += strlen(token);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return false;
    *out = (int)atoi(p);
    return true;
}

/* ------------------------------------------------------------------ */
/* Payload parsing                                                      */
/* ------------------------------------------------------------------ */

bool ProvisioningManager::parseProvisionPayload(const uint8_t* payload, size_t len) {
    // Treat payload as a null-terminated string (safe copy)
    char buf[256];
    size_t copy_len = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
    memcpy(buf, payload, copy_len);
    buf[copy_len] = '\0';

    // version must be 1
    int version = 0;
    if (!jsonGetInt(buf, "version", &version) || version != 1) return false;

    // tier must be "squad"
    char tier[16];
    if (!jsonGetString(buf, "tier", tier, sizeof(tier))) return false;
    if (strcmp(tier, "squad") != 0) return false;

    // callsign
    char callsign[17];
    if (!jsonGetString(buf, "callsign", callsign, sizeof(callsign))) return false;

    // role
    int role = 0;
    if (!jsonGetInt(buf, "role", &role)) return false;
    if (role < 0 || role > 3) return false;

    // key (base64 → 32 bytes)
    char key_b64[64];
    if (!jsonGetString(buf, "key", key_b64, sizeof(key_b64))) return false;
    uint8_t psk[32];
    int decoded = base64Decode(key_b64, psk, sizeof(psk));
    if (decoded != 32) return false;

    // All checks passed — populate result
    memset(&result, 0, sizeof(result));
    strncpy(result.callsign, callsign, 16);
    result.callsign[16] = '\0';
    result.role          = (uint8_t)role;
    result.interval_secs = 30;  // default
    result.channel_idx   = 0;
    memcpy(result.squad_psk, psk, 32);
    result.mode          = NODE_MODE_TRACKER;  // default — caller may override to RELAY

    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void ProvisioningManager::begin(unsigned long now_ms) {
    memset(&result, 0, sizeof(result));
    _window_open     = false;
    _provisioned     = false;
    _window_start_ms = 0;
    _pending_mode    = NODE_MODE_UNPROVISIONED;
    _led_pattern     = LED_PAT_NONE;
    _led_next_ms     = 0;
    _led_blink_count = 0;
    _led_state       = false;

#if PROV_LED_PIN >= 0
    pinMode(PROV_LED_PIN, OUTPUT);
    ledOff();
#endif

    // Open the window immediately on begin
    _window_open     = true;
    _window_start_ms = now_ms;
    setLEDPattern(LED_PAT_SLOW_PULSE, now_ms);
}

void ProvisioningManager::loop(unsigned long now_ms) {
    indicateLED(now_ms);

    if (!_window_open) return;

    // Check for timeout
    if ((unsigned long)(now_ms - _window_start_ms) >= PROVISION_WINDOW_MS) {
        // Timed out — default to relay mode
        closeWindow(NODE_MODE_RELAY);
    }
}

void ProvisioningManager::closeWindow(NodeMode mode) {
    _window_open  = false;
    _provisioned  = true;
    _pending_mode = mode;
    result.mode   = mode;

    unsigned long now_ms = (unsigned long)millis();
    if (mode == NODE_MODE_TRACKER) {
        setLEDPattern(LED_PAT_TRACKER_OK, now_ms);
    } else {
        setLEDPattern(LED_PAT_RELAY_TIMEOUT, now_ms);
    }
}

void ProvisioningManager::onNFCProvision(const uint8_t* payload, size_t len) {
    if (!_window_open) return;

    unsigned long now_ms = (unsigned long)millis();
    setLEDPattern(LED_PAT_TRIPLE_BLINK, now_ms);

    if (parseProvisionPayload(payload, len)) {
        result.mode = NODE_MODE_TRACKER;
        closeWindow(NODE_MODE_TRACKER);
    }
    // If parse fails, window stays open — keep waiting
}

void ProvisioningManager::onBLEProvision(const uint8_t* payload, size_t len) {
    if (!_window_open) return;

    unsigned long now_ms = (unsigned long)millis();
    setLEDPattern(LED_PAT_TRIPLE_BLINK, now_ms);

    if (parseProvisionPayload(payload, len)) {
        result.mode = NODE_MODE_TRACKER;
        closeWindow(NODE_MODE_TRACKER);
    }
}

void ProvisioningManager::requestReopen() {
    unsigned long now_ms = (unsigned long)millis();
    _window_open     = true;
    _provisioned     = false;
    _window_start_ms = now_ms;
    _pending_mode    = NODE_MODE_UNPROVISIONED;
    setLEDPattern(LED_PAT_REPROVISION, now_ms);
}
