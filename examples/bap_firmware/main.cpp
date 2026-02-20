#include <Arduino.h>
#include <Mesh.h>
#include "MyMesh.h"
#include "BAPConfig.h"
#include "BAPScreen.h"
#include "BAPAPI.h"
#include <target.h>
#include <WiFi.h>

// Global objects
StdRNG fast_rng;
SimpleMeshTables tables;
BAPMesh* the_mesh = nullptr;
BAPConfigManager config_mgr;
BAPConfig bap_config;
BAPScreen* screen = nullptr;
BAPAPIClient* api_client = nullptr;

// State tracking
uint32_t arrival_sequence = 0;
BusArrival current_arrivals[5];
int current_arrival_count = 0;
uint32_t arrivals_generated_at = 0;

// Serial command buffer
static char command[160];

void halt() {
  while (1) delay(1000);
}

// Callback when arrivals are received from mesh
void onArrivalsReceived(const BusArrival* arrivals, int count, uint32_t generated_at) {
  MESH_DEBUG_PRINTLN("Received %d arrivals from mesh", count);

  // Filter arrivals for our stop ID
  int filtered_count = 0;
  for (int i = 0; i < count && filtered_count < 5; i++) {
    if (arrivals[i].stop_id == bap_config.stop_id || bap_config.stop_id == 0) {
      current_arrivals[filtered_count] = arrivals[i];
      filtered_count++;
    }
  }

  if (filtered_count > 0) {
    current_arrival_count = filtered_count;
    arrivals_generated_at = generated_at;

    // Update display
    if (screen) {
      screen->update(bap_config.stop_id, current_arrivals, current_arrival_count, arrivals_generated_at, 0);
    }
  }
}

// Gateway task - poll API and broadcast to mesh
void gatewayTask() {
  static unsigned long last_check = 0;
  static bool time_synced = false;

  if (millis() - last_check < 1000) return;  // Check every second
  last_check = millis();

  if (!api_client || !the_mesh) return;

  // Check WiFi status
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long last_wifi_msg = 0;
    if (millis() - last_wifi_msg > 10000) {  // Only print every 10s
      Serial.println("WiFi not connected, waiting...");
      last_wifi_msg = millis();
    }
    return;
  }

  // Sync time once after WiFi connects
  if (!time_synced) {
    Serial.println("WiFi connected! Syncing time...");
    configTime(-8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    time_synced = true;
    if (screen) {
      screen->showMessage("Gateway Mode", "Connected!");
    }
  }

  // Check if it's time to poll the API
  if (!api_client->shouldPoll()) {
    return;
  }

  Serial.println("[GATEWAY] Polling API for arrivals...");
  Serial.printf("[GATEWAY] Stop ID: %u, API Key: %s\n", bap_config.stop_id,
                bap_config.api_key[0] ? "set" : "NOT SET");

  MESH_DEBUG_PRINTLN("Polling API for arrivals...");

  // Fetch arrivals for our configured stop
  BusArrival arrivals[BAP_MAX_ARRIVALS];
  int count = api_client->fetchArrivals(bap_config.stop_id, arrivals, BAP_MAX_ARRIVALS);

  if (count > 0) {
    MESH_DEBUG_PRINTLN("Fetched %d arrivals from API", count);

    // Update our local display
    int display_count = (count > 5) ? 5 : count;
    for (int i = 0; i < display_count; i++) {
      current_arrivals[i] = arrivals[i];
    }
    current_arrival_count = display_count;
    arrivals_generated_at = time(nullptr);

    if (screen) {
      screen->update(bap_config.stop_id, current_arrivals, current_arrival_count, arrivals_generated_at, 0);
    }

    // Broadcast to mesh
    the_mesh->sendArrivals(arrivals, count, ++arrival_sequence);
  } else if (count < 0) {
    // Mark polled even on error to prevent spamming
    api_client->markPolled();
    MESH_DEBUG_PRINTLN("API error: %s", api_client->getLastError());
  }
}

// Display task - show current state
void displayTask() {
  static unsigned long last_update = 0;

  // Update display every 30 seconds to show staleness indicator
  if (millis() - last_update < 30000) return;
  last_update = millis();

  if (!screen || current_arrival_count == 0) return;

  // Check if data is stale
  uint32_t now = time(nullptr);
  if (now - arrivals_generated_at > BAP_STALE_THRESHOLD) {
    // Show stale indicator on display
    screen->update(bap_config.stop_id, current_arrivals, current_arrival_count, arrivals_generated_at, 0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== BAP Firmware Starting ===");
  Serial.printf("Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Build: %s\n", FIRMWARE_BUILD_DATE);
  Serial.println("[1] Serial initialized");

  // Initialize board
  Serial.println("[2] Initializing board...");
  board.begin();
  Serial.println("[2] Board initialized");

#ifdef DISPLAY_CLASS
  Serial.println("[3] Initializing display...");
  if (display.begin()) {
    display.startFrame();
    display.setTextSize(2);
    display.drawTextCentered(display.width() / 2, 28, "BAP Firmware");
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 50, FIRMWARE_VERSION);
    display.endFrame();
    Serial.println("[3] Display initialized");
  } else {
    Serial.println("[3] Display init failed!");
  }
#endif

  // Initialize radio
  Serial.println("[4] Initializing radio...");
  if (!radio_init()) {
    Serial.println("[4] Radio init FAILED!");
#ifdef DISPLAY_CLASS
    display.startFrame();
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 50, "Radio Init Failed!");
    display.endFrame();
#endif
    halt();
  }
  Serial.println("[4] Radio initialized");

  // Initialize RNG
  Serial.println("[5] Initializing RNG...");
  fast_rng.begin(radio_get_rng_seed());
  Serial.println("[5] RNG initialized");

  // Initialize SPIFFS
  Serial.println("[6] Initializing SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("[6] SPIFFS init FAILED!");
  } else {
    Serial.println("[6] SPIFFS initialized");
  }

  // Load configuration
  Serial.println("[7] Loading configuration...");
  if (!config_mgr.load(bap_config)) {
    Serial.println("Using default configuration");
  }
  config_mgr.printConfig(bap_config, Serial);

  // Create mesh instance
  Serial.println("[8] Creating mesh...");
  the_mesh = new BAPMesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);
  Serial.println("[8] Mesh created, calling begin...");
  the_mesh->begin(&SPIFFS);
  Serial.println("[8] Mesh begin complete");
  the_mesh->onArrivalsReceived = onArrivalsReceived;

  // Create display handler
  Serial.println("[9] Creating screen handler...");
#ifdef DISPLAY_CLASS
  screen = new BAPScreen(&display);
  screen->begin();

  // Set stop name based on stop_id (in production, this would be looked up)
  char stop_name[32];
  snprintf(stop_name, sizeof(stop_name), "Stop %u", bap_config.stop_id);
  screen->setStopName(stop_name);
  Serial.println("[9] Screen handler created");
#endif

  // Determine role and initialize accordingly
  Serial.println("[10] Determining role...");
  if (bap_config.wifi_ssid[0] != '\0') {
    // Gateway mode
    Serial.println("[10] Starting in GATEWAY mode");
    Serial.printf("[10] WiFi SSID: %s\n", bap_config.wifi_ssid);

    api_client = new BAPAPIClient();

    // Set API key if configured
    if (bap_config.api_key[0] != '\0') {
      api_client->setApiKey(bap_config.api_key);
      Serial.println("[10] API key configured");
    } else {
      Serial.println("[10] WARNING: API key not set - use 'setapikey <key>' command");
    }

    api_client->setPollInterval(60000);  // Poll every 60 seconds

    // Connect to WiFi in background (non-blocking)
    Serial.println("[10] Starting WiFi connection (async)...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(bap_config.wifi_ssid, bap_config.wifi_password);
    board.setInhibitSleep(true);

    if (screen) {
      screen->showMessage("Gateway Mode", "Connecting WiFi...");
    }
  } else {
    // Display mode
    Serial.println("[10] Starting in DISPLAY mode");

    if (screen) {
      screen->showMessage("Display Mode", "Waiting for data...");
    }
  }

  // Initialize sensors
  sensors.begin();

  // Send initial advertisement (optional - helps other nodes discover us)
#if ENABLE_ADVERT_ON_BOOT == 1
  // Create and send a simple advertisement packet
  // For now, we skip this as BAPMesh doesn't implement the full advertisement system
  // the_mesh->sendSelfAdvertisement(16000, false);
#endif

  Serial.println("Setup complete!");
}

void loop() {
  // Handle serial commands
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command) - 1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }

  if (len > 0 && command[len - 1] == '\r') {
    Serial.println();
    command[len - 1] = 0;

    char reply[160];
    config_mgr.handleCommand(command, reply, bap_config);
    if (reply[0]) {
      Serial.print("-> ");
      Serial.println(reply);
    }

    // Check for config changes that require action
    if (strncmp(command, "setwifi", 7) == 0 || strcmp(command, "clearwifi") == 0) {
      // Role changed - would need to restart to take effect
      Serial.println("Configuration changed. Restart to apply.");
    }

    command[0] = 0;
  }

  // Run mesh loop
  the_mesh->loop();

  // Run sensors loop
  sensors.loop();

  // Run role-specific tasks
  if (the_mesh->isGateway() && api_client) {
    gatewayTask();
  } else {
    displayTask();
  }

  // Update RTC
  rtc_clock.tick();
}
