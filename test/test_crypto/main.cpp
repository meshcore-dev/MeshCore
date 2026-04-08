#include <unity.h>
#include "test_utils.h"
#include "test_main_common.h"

static bool tests_run = false;

extern "C" void unity_putchar_(int c) {
  Serial.write((uint8_t)c);
}

void setup(void) {
  // Initialize Serial first
  Serial.begin(115200);
  delay(2000);

  // Output header
  Serial.println("\n\nTEST_START");
  Serial.flush();

  DBG_INFO("╔════════════════════════════════════════════════════╗");
  DBG_INFO("║   MeshCore Crypto Test Suite (Arduino)             ║");
  DBG_INFO("╚════════════════════════════════════════════════════╝");
  DBG_INFO("");

  UNITY_BEGIN();

  run_all_tests();

  UNITY_END();
  tests_run = true;

  Serial.println("\n");
  Serial.println("═══════════════════════════════════════════════════════");
  Serial.println("TEST_DONE");
  Serial.println("═══════════════════════════════════════════════════════");
}

void loop(void) {
  if (tests_run) {
    // Tests are complete, just loop
    delay(1000);
  }
}
