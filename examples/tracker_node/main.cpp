#include <Arduino.h>
#include <Mesh.h>

// Filesystem setup — platform-specific
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#else
  #error "unsupported platform — add filesystem include"
#endif

// Display
#ifdef DISPLAY_CLASS
  #include <helpers/ui/ST7735Display.h>
#endif

#include "TrackerMesh.h"
#include "RelayMesh.h"
#include "ProvisioningManager.h"
#include "BLEProvInterface.h"

#ifdef NRF52_PLATFORM
  #include "NFCTagHandler.h"
#endif

/* ------------------------------------------------------------------ */
/* Hardware peripherals & mesh instances                                */
/* ------------------------------------------------------------------ */

StdRNG         fast_rng;
SimpleMeshTables tables;

TrackerMesh    the_mesh(radio_driver, fast_rng, rtc_clock, tables);
RelayMesh      relay_mesh(radio_driver, fast_rng, rtc_clock, tables);

ProvisioningManager prov_manager;
BLEProvInterface    ble_prov;

#ifdef NRF52_PLATFORM
NFCTagHandler nfc_handler;
#endif

/* ------------------------------------------------------------------ */
/* Runtime state                                                        */
/* ------------------------------------------------------------------ */

static NodeMode current_mode    = NODE_MODE_UNPROVISIONED;
static FILESYSTEM* fs           = nullptr;
static char command[160];

// Button hold tracking — 5 s for re-provision request
#ifdef BUTTON_PIN
static unsigned long btn_down_at = 0;
static bool          btn_reprove_sent = false;
static const uint32_t BTN_HOLD_MS = 5000;
#endif

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void halt() {
    while (1) ;
}

static void loadModeFromPrefs(FILESYSTEM* filesystem) {
    // TrackerMesh exposes loadPrefs indirectly — read the raw prefs file directly
    // so we know the mode before deciding which mesh to start.
    TrackerPrefs prefs;
    memset(&prefs, 0, sizeof(prefs));

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    File f = filesystem->open(TRACKER_PREFS_FILENAME, FILE_O_READ);
#else
    File f = filesystem->open(TRACKER_PREFS_FILENAME, "r");
#endif
    if (f) {
        f.read((uint8_t*)&prefs, sizeof(prefs));
        f.close();
        current_mode = prefs.mode;
    } else {
        current_mode = NODE_MODE_UNPROVISIONED;
    }
}

static void saveProvisionedPrefs(FILESYSTEM* filesystem, const TrackerPrefs& prefs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    File f = filesystem->open(TRACKER_PREFS_FILENAME, FILE_O_WRITE);
#else
    File f = filesystem->open(TRACKER_PREFS_FILENAME, "w");
#endif
    if (f) {
        f.write((const uint8_t*)&prefs, sizeof(prefs));
        f.close();
    }
}

static bool isBtnHeldAtBoot() {
#ifdef BUTTON_PIN
    // Sample the button a few times with a short delay to debounce
    uint8_t pressed = 0;
    for (int i = 0; i < 5; i++) {
        if (digitalRead(BUTTON_PIN) == HIGH) pressed++;
        delay(10);
    }
    return (pressed >= 4);
#else
    return false;
#endif
}

/* ------------------------------------------------------------------ */
/* setup()                                                              */
/* ------------------------------------------------------------------ */

void setup() {
    Serial.begin(115200);
    delay(500);

    board.begin();

#ifdef DISPLAY_CLASS
    if (display.begin()) {
        display.startFrame();
        display.print("Kestrel Boot...");
        display.endFrame();
    }
#endif

    if (!radio_init()) { halt(); }

    fast_rng.begin(radio_get_rng_seed());

    // ---- Filesystem ----
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    InternalFS.begin();
    fs = &InternalFS;
    IdentityStore store(InternalFS, "");
#elif defined(ESP32)
    SPIFFS.begin(true);
    fs = &SPIFFS;
    IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
    LittleFS.begin();
    fs = &LittleFS;
    IdentityStore store(LittleFS, "/identity");
    store.begin();
#else
  #error "filesystem not defined"
#endif

    // ---- Identity ----
    if (!store.load("_main", the_mesh.self_id)) {
        MESH_DEBUG_PRINTLN("Generating new identity");
        the_mesh.self_id = radio_new_identity();
        int tries = 0;
        while (tries < 10 &&
               (the_mesh.self_id.pub_key[0] == 0x00 ||
                the_mesh.self_id.pub_key[0] == 0xFF)) {
            the_mesh.self_id = radio_new_identity();
            tries++;
        }
        store.save("_main", the_mesh.self_id);
    }
    // Share the same identity with relay mesh
    relay_mesh.self_id = the_mesh.self_id;

    Serial.print("Node ID: ");
    mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();

    // ---- Read persisted mode ----
    loadModeFromPrefs(fs);

    // ---- Button held at boot → force re-provision ----
    if (isBtnHeldAtBoot()) {
        current_mode = NODE_MODE_UNPROVISIONED;
        Serial.println("Boot button held — forcing provisioning window");
    }

    // ---- Branch on mode ----
    if (current_mode == NODE_MODE_UNPROVISIONED) {
        Serial.println("Mode: UNPROVISIONED — opening 60s provisioning window");

#ifdef DISPLAY_CLASS
        display.startFrame();
        display.print("Provisioning...");
        display.endFrame();
#endif

        prov_manager.begin(millis());

        // Start BLE advertising for provisioning (nRF52 always; stub on ESP32)
        ble_prov.begin();

        // Start NFC tag emulation (nRF52 only)
#ifdef NRF52_PLATFORM
        nfc_handler.begin();
#endif

    } else if (current_mode == NODE_MODE_TRACKER) {
        Serial.println("Mode: TRACKER");

#ifdef DISPLAY_CLASS
        display.startFrame();
        display.print("Tracker Mode");
        display.endFrame();
#endif

        sensors.begin();
        the_mesh.begin(fs);

        command[0] = 0;
        Serial.println("Kestrel TrackerMesh ready.");

#if ENV_INCLUDE_GPS == 1
        sensors.setSettingValue("gps", "1");
#endif

    } else if (current_mode == NODE_MODE_RELAY) {
        Serial.println("Mode: RELAY");

#ifdef DISPLAY_CLASS
        display.startFrame();
        display.print("Relay Mode");
        display.endFrame();
#endif

        // GPS and sensors off in relay mode
        relay_mesh.begin();
        command[0] = 0;
        Serial.println("Kestrel RelayMesh ready.");

    } else {
        // Unknown mode value — treat as unprovisioned
        current_mode = NODE_MODE_UNPROVISIONED;
        prov_manager.begin(millis());
        ble_prov.begin();
#ifdef NRF52_PLATFORM
        nfc_handler.begin();
#endif
    }
}

/* ------------------------------------------------------------------ */
/* loop()                                                               */
/* ------------------------------------------------------------------ */

void loop() {
    unsigned long now = millis();

    // ---- Button hold detection (re-provision) ----
#ifdef BUTTON_PIN
    if (current_mode != NODE_MODE_UNPROVISIONED) {
        uint8_t btn = digitalRead(BUTTON_PIN);
        if (btn == HIGH) {
            if (btn_down_at == 0) {
                btn_down_at     = now;
                btn_reprove_sent = false;
            } else if (!btn_reprove_sent && (unsigned long)(now - btn_down_at) >= BTN_HOLD_MS) {
                btn_reprove_sent = true;
                Serial.println("Button held 5s — re-provisioning");
                prov_manager.requestReopen();
                ble_prov.begin();
#ifdef NRF52_PLATFORM
                nfc_handler.begin();
#endif
                current_mode = NODE_MODE_UNPROVISIONED;
            }
        } else {
            btn_down_at      = 0;
            btn_reprove_sent = false;
        }
    }
#endif

    // ---- Provisioning window active ----
    if (current_mode == NODE_MODE_UNPROVISIONED) {
        prov_manager.loop(now);

        // Check NFC (nRF52 only)
#ifdef NRF52_PLATFORM
        if (nfc_handler.hasNewData()) {
            const uint8_t* raw   = nfc_handler.getData();
            size_t         raw_len = nfc_handler.getDataLen();
            nfc_handler.clearNewData();

            // Parse NDEF — extract payload from first record
            size_t payload_len = 0;
            // NDEF message from iOS NFC writer: skip TLV wrapper (T=0x03, L, ...)
            // then parse the NDEF record header to get the JSON payload.
            // Minimal TLV unwrap: if buf[0]==0x03 (NDEF TLV), skip T+L bytes.
            const uint8_t* ndef = raw;
            size_t ndef_len = raw_len;
            if (raw_len > 2 && raw[0] == 0x03) {
                // short TLV: L is raw[1]
                ndef     = raw + 2;
                ndef_len = raw[1];
                if (ndef_len > raw_len - 2) ndef_len = raw_len - 2;
            }

            // NDEF record: flags[0], type_len[1], payload_len[2 or 2-5]
            if (ndef_len >= 3) {
                bool  sr           = (ndef[0] & 0x10) != 0;
                uint8_t type_len   = ndef[1];
                size_t  hdr_off    = 2;
                uint32_t plen;
                if (sr) {
                    plen = ndef[hdr_off++];
                } else {
                    if (hdr_off + 4 > ndef_len) goto ndef_skip;
                    plen = ((uint32_t)ndef[hdr_off]   << 24) | ((uint32_t)ndef[hdr_off+1] << 16) |
                           ((uint32_t)ndef[hdr_off+2] <<  8) |  (uint32_t)ndef[hdr_off+3];
                    hdr_off += 4;
                }
                if (ndef[0] & 0x08) hdr_off++;  // skip IL byte
                hdr_off += type_len;
                if (hdr_off + plen <= ndef_len) {
                    prov_manager.onNFCProvision(ndef + hdr_off, (size_t)plen);
                }
            }
            ndef_skip:;
        }
#endif

        // Check BLE RX
        if (ble_prov.hasData()) {
            prov_manager.onBLEProvision(ble_prov.getData(), ble_prov.getDataLen());
            ble_prov.clearData();
        }

        // Check if provisioning resolved
        if (prov_manager.isProvisioned()) {
            NodeMode new_mode = prov_manager.getMode();

            // Stop provisioning interfaces
            ble_prov.stop();
#ifdef NRF52_PLATFORM
            nfc_handler.stop();
#endif

            // Save prefs to NVM
            TrackerPrefs prefs = prov_manager.result;
            prefs.mode         = new_mode;
            prefs.interval_secs = 30;
            if (prefs.interval_secs < 10) prefs.interval_secs = 10;
            saveProvisionedPrefs(fs, prefs);

            current_mode = new_mode;

            if (new_mode == NODE_MODE_TRACKER) {
                Serial.printf("Provisioned: TRACKER callsign=%s role=%u\n",
                              prefs.callsign, (unsigned)prefs.role);
                sensors.begin();
                the_mesh.begin(fs);
#if ENV_INCLUDE_GPS == 1
                sensors.setSettingValue("gps", "1");
#endif
            } else {
                Serial.println("Provisioned: RELAY (timeout default)");
                relay_mesh.begin();
            }
        }
        return;  // Don't fall through to normal mesh ops while provisioning
    }

    // ---- Serial CLI ----
    int len = strlen(command);
    while (Serial.available() && len < (int)sizeof(command) - 1) {
        char c = Serial.read();
        if (c != '\n') {
            command[len++] = c;
            command[len]   = 0;
        }
        Serial.print(c);
    }
    if (len == (int)sizeof(command) - 1) {
        command[sizeof(command) - 1] = '\r';
    }

    if (len > 0 && command[len - 1] == '\r') {
        command[len - 1] = 0;
        char reply[160];
        reply[0] = 0;
        if (current_mode == NODE_MODE_TRACKER) {
            the_mesh.handleCommand(command, reply);
        } else if (current_mode == NODE_MODE_RELAY) {
            relay_mesh.handleCommand(command, reply);
        }
        if (reply[0]) {
            Serial.print("  -> ");
            Serial.println(reply);
        }
        command[0] = 0;
    }

    // ---- Mesh operation ----
    if (current_mode == NODE_MODE_TRACKER) {
        the_mesh.loop();
        sensors.loop();
    } else if (current_mode == NODE_MODE_RELAY) {
        relay_mesh.loop();
        // sensors stay off in relay mode
    }

    rtc_clock.tick();
}
