#include <Arduino.h>
#include <Mesh.h>
#include "TrackerMesh.h"

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

// Display — Heltec Tracker V2 has ST7735 TFT
#ifdef DISPLAY_CLASS
  #include <helpers/ui/ST7735Display.h>
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

TrackerMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables);

static char command[160];

void halt() {
    while (1) ;
}

void setup() {
    Serial.begin(115200);
    delay(500);

    board.begin();

#ifdef DISPLAY_CLASS
    if (display.begin()) {
        display.startFrame();
        display.print("Kestrel Tracker");
        display.endFrame();
    }
#endif

    if (!radio_init()) { halt(); }

    fast_rng.begin(radio_get_rng_seed());

    // ---- Filesystem + identity ----
    FILESYSTEM* fs;

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

    if (!store.load("_main", the_mesh.self_id)) {
        MESH_DEBUG_PRINTLN("Generating new identity");
        the_mesh.self_id = radio_new_identity();
        int tries = 0;
        while (tries < 10 &&
               (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
            the_mesh.self_id = radio_new_identity();
            tries++;
        }
        store.save("_main", the_mesh.self_id);
    }

    Serial.print("Tracker ID: ");
    mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();

    sensors.begin();

    the_mesh.begin(fs);

    command[0] = 0;

    Serial.println("Kestrel TrackerMesh ready. Type 'status' for info.");

#if ENV_INCLUDE_GPS == 1
    // enable GPS
    sensors.setSettingValue("gps", "1");
#endif
}

void loop() {
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
        command[sizeof(command) - 1] = '\r';  // force flush on overflow
    }

    if (len > 0 && command[len - 1] == '\r') {
        command[len - 1] = 0;
        char reply[160];
        reply[0] = 0;
        the_mesh.handleCommand(command, reply);
        if (reply[0]) {
            Serial.print("  -> ");
            Serial.println(reply);
        }
        command[0] = 0;
    }

    the_mesh.loop();
    sensors.loop();
    rtc_clock.tick();
}
