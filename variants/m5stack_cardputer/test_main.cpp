#include <Arduino.h>
#include <Mesh.h>

#include "MyMesh.h"

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) {
    delay(1000);
  }
}

static char command[160];
static uint32_t last_test_packet_time = 0;
static uint32_t test_packet_count = 0;
static uint32_t test_packet_success = 0;

#ifdef RADIO_TEST_MODE
// Radio self-test function
void radioSelfTest() {
  Serial.println("\n=== RADIO SELF-TEST ===");
  
  // Test 1: Check radio initialization
  Serial.print("Radio initialized: ");
  Serial.println("OK");
  
  // Test 2: Get random seed (verifies radio communication)
  uint32_t seed = radio_get_rng_seed();
  Serial.print("RNG seed: 0x");
  Serial.println(seed, HEX);
  
  // Test 3: Check radio parameters
  Serial.println("\nRadio Configuration:");
  Serial.print("  Frequency: ");
  Serial.print(LORA_FREQ);
  Serial.println(" MHz");
  Serial.print("  Bandwidth: ");
  Serial.print(LORA_BW);
  Serial.println(" kHz");
  Serial.print("  Spreading Factor: ");
  Serial.println(LORA_SF);
  Serial.print("  Coding Rate: 4/");
  Serial.println(LORA_CR);
  Serial.print("  TX Power: ");
  Serial.print(LORA_TX_POWER);
  Serial.println(" dBm");
  
  Serial.println("\n=== SELF-TEST COMPLETE ===\n");
}

// Periodic test packet sender
void sendTestPacket() {
  if (millis() - last_test_packet_time > 30000) {  // Every 30 seconds
    last_test_packet_time = millis();
    test_packet_count++;
    
    Serial.print("[TEST] Sending test packet #");
    Serial.println(test_packet_count);
    
    // Send a test advertisement
    bool success = the_mesh.sendSelfAdvertisement(16000);
    if (success) {
      test_packet_success++;
      Serial.println("[TEST] Packet sent successfully");
    } else {
      Serial.println("[TEST] Packet send FAILED");
    }
    
    Serial.print("[TEST] Success rate: ");
    Serial.print(test_packet_success);
    Serial.print("/");
    Serial.print(test_packet_count);
    Serial.print(" (");
    Serial.print((test_packet_success * 100) / test_packet_count);
    Serial.println("%)");
    
    #ifdef DISPLAY_CLASS
      display.startFrame();
      display.setCursor(0, 80);
      display.setTextSize(1);
      display.setTextColor(YELLOW, BLACK);
      display.print("Test: ");
      display.print(test_packet_success);
      display.print("/");
      display.print(test_packet_count);
      display.endFrame();
    #endif
  }
}
#endif

void setup() {
  Serial.begin(115200);
  delay(2000);  // Give time for serial monitor to connect
  
  Serial.println("\n\n=== MESHCORE M5STACK CARDPUTER-ADV ===");
  Serial.println("Hardware: M5Stack Cardputer-Adv");
  Serial.println("Radio: DX-LR30-900M22SP (SX1262)");
  Serial.print("Firmware: ");
  Serial.println(FIRMWARE_VERSION);
  Serial.print("Build: ");
  Serial.println(FIRMWARE_BUILD_DATE);

  board.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.clear();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.setTextColor(GREEN, BLACK);
    display.print("MESHCORE");
    display.setCursor(0, 20);
    display.setTextSize(1);
    display.setTextColor(WHITE, BLACK);
    display.print("Initializing...");
    display.endFrame();
  }
#endif

  Serial.println("\nInitializing SX1262 radio...");
  if (!radio_init()) {
    Serial.println("ERROR: Radio initialization FAILED!");
    Serial.println("Check wiring:");
    Serial.println("  NSS  -> GPIO 5");
    Serial.println("  DIO1 -> GPIO 4");
    Serial.println("  RST  -> GPIO 3");
    Serial.println("  BUSY -> GPIO 6");
    Serial.println("  MOSI -> GPIO 14");
    Serial.println("  MISO -> GPIO 39");
    Serial.println("  SCK  -> GPIO 40");
    
    #ifdef DISPLAY_CLASS
      display.startFrame();
      display.setCursor(0, 40);
      display.setTextColor(RED, BLACK);
      display.print("RADIO INIT FAIL!");
      display.endFrame();
    #endif
    
    halt();
  }
  
  Serial.println("Radio initialized successfully!");

#ifdef RADIO_TEST_MODE
  radioSelfTest();
#endif

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#else
  #error "need to define filesystem"
#endif
  
  if (!store.load("_main", the_mesh.self_id)) {
    Serial.println("Generating new identity keypair...");
    the_mesh.self_id = radio_new_identity();
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
      the_mesh.self_id = radio_new_identity(); 
      count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Node ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); 
  Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
  
  display.startFrame();
  display.clear();
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.setTextColor(GREEN, BLACK);
  display.print("MESHCORE");
  display.setCursor(0, 20);
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  display.print("Ready!");
  display.setCursor(0, 35);
  display.print("868.0 MHz SF");
  display.print(LORA_SF);
  display.setCursor(0, 50);
  display.print("Battery: ");
  display.print(board.getBattMilliVolts());
  display.print("mV");
  display.endFrame();
#endif

  Serial.println("\n=== SYSTEM READY ===");
  Serial.println("Type 'help' for commands");
  Serial.println("Listening for mesh packets...\n");

  // Send initial advertisement
  the_mesh.sendSelfAdvertisement(16000);
}

void loop() {
  // Handle serial commands
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
      Serial.print("  -> "); 
      Serial.println(reply);
    }
    command[0] = 0;
  }

  the_mesh.loop();
  sensors.loop();
  
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif

  rtc_clock.tick();

#ifdef RADIO_TEST_MODE
  sendTestPacket();
#endif
}
