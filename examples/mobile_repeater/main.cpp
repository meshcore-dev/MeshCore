#include <Arduino.h>
#include <Mesh.h>
#include "../simple_repeater/MyMesh.h"
#include "MobilityWatchdog.h"

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);
static MobilityWatchdog watchdog(the_mesh);

void halt() {
  while (1) ;
}

static char command[160];
unsigned long lastActive = 0;
unsigned long nextSleepinSecs = 120;

void setup() {
  Serial.begin(115200);

  board.begin();

  if (!radio_init()) {
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

  LittleFS.begin();
  FILESYSTEM* fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();

  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  command[0] = 0;
  lastActive = millis();

  sensors.begin();
  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  watchdog.begin();

#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#endif
}

void loop() {
  // Serial CLI
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {
    command[sizeof(command)-1] = '\r';
  }
  if (len > 0 && command[len - 1] == '\r') {
    Serial.print('\n');
    command[len - 1] = 0;
    char reply[160];
    the_mesh.handleCommand(0, command, reply);
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }
    command[0] = 0;
  }

  the_mesh.loop();
  sensors.loop();
  watchdog.loop();

#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif

  rtc_clock.tick();

  if (the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork()) {
    if (the_mesh.millisHasNowPassed(lastActive + nextSleepinSecs * 1000)) {
      board.sleep(1800);
      lastActive = millis();
      nextSleepinSecs = 5;
    } else {
      nextSleepinSecs += 5;
    }
  }
}