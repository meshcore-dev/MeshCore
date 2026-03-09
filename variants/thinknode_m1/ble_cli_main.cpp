// Simple BLE CLI firmware for Thinknode M1
// This lightweight firmware provides CLI access via Bluetooth
// Primary purpose: Enter OTA mode wirelessly
// Flash this firmware if you need to update to another firmware via BLE OTA

#include <Arduino.h>
#include <bluefruit.h>
#include "ThinkNodeM1Board.h"

BLEUart bleuart;
ThinkNodeM1Board board;

#define CMD_BUFFER_SIZE 128
char cmd_buffer[CMD_BUFFER_SIZE];
uint8_t cmd_pos = 0;

void processCommand(const char* cmd);
void sendPrompt();

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) delay(10);  // Wait up to 3 seconds for serial
  
  Serial.println("\n===========================================");
  Serial.println("ThinkNode M1 - BLE CLI Mode");
  Serial.println("===========================================");
  Serial.println("Initializing...");
  
  // Initialize board
  board.begin();
  
  // Configure and start BLE
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin();
  Bluefruit.setTxPower(4);  // Max power
  Bluefruit.setName("THINKNODE_M1_CLI");
  
  // Start BLE UART service
  bleuart.begin();
  
  // Set up advertising
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(bleuart);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.start(0);  // 0 = never stop advertising
  
  Serial.println("BLE CLI ready!");
  Serial.println("Device name: THINKNODE_M1_CLI");
  Serial.println("");
  Serial.println("Connect via Bluetooth terminal app");
  Serial.println("(nRF Connect, BlueTerm, Serial Bluetooth Terminal, etc.)");
  Serial.println("");
  Serial.println("Available commands:");
  Serial.println("  start ota  - Enter OTA firmware update mode");
  Serial.println("  help       - Show this help");
  Serial.println("  info       - Show device information");
  Serial.println("  reboot     - Reboot device");
  Serial.println("===========================================");
}

void loop() {
  // Check for BLE UART data
  while (bleuart.available()) {
    char c = bleuart.read();
    
    // Echo character
    if (c >= 32 && c < 127) {
      bleuart.write(c);
    }
    
    // Handle backspace
    if (c == '\b' || c == 0x7F) {
      if (cmd_pos > 0) {
        cmd_pos--;
        bleuart.write("\b \b");
      }
      continue;
    }
    
    // Handle enter/return
    if (c == '\r' || c == '\n') {
      bleuart.write("\r\n");
      
      if (cmd_pos > 0) {
        cmd_buffer[cmd_pos] = '\0';
        processCommand(cmd_buffer);
        cmd_pos = 0;
      }
      
      sendPrompt();
      continue;
    }
    
    // Add to buffer
    if (cmd_pos < CMD_BUFFER_SIZE - 1 && c >= 32 && c < 127) {
      cmd_buffer[cmd_pos++] = c;
    }
  }
  
  // Also handle serial commands for debugging
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\r' || c == '\n') {
      if (cmd_pos > 0) {
        cmd_buffer[cmd_pos] = '\0';
        processCommand(cmd_buffer);
        cmd_pos = 0;
      }
      Serial.print("\n> ");
      continue;
    }
    
    if (c == '\b' || c == 0x7F) {
      if (cmd_pos > 0) {
        cmd_pos--;
        Serial.write("\b \b");
      }
      continue;
    }
    
    if (cmd_pos < CMD_BUFFER_SIZE - 1 && c >= 32 && c < 127) {
      cmd_buffer[cmd_pos++] = c;
      Serial.write(c);
    }
  }
  
  delay(10);
}

void sendPrompt() {
  bleuart.write("> ");
}

void processCommand(const char* cmd) {
  char reply[256];
  reply[0] = '\0';
  
  // Trim leading/trailing spaces
  while (*cmd == ' ') cmd++;
  
  if (strlen(cmd) == 0) {
    return;
  }
  
  Serial.printf("Command: %s\n", cmd);
  
  if (strcmp(cmd, "start ota") == 0) {
    bleuart.println("Entering OTA mode...");
    bleuart.println("Device will now advertise as: THINKNODE_M1_OTA");
    bleuart.println("Use nRF DFU app to upload firmware.");
    bleuart.flush();
    delay(500);
    
    Serial.println("Starting OTA mode...");
    
    if (board.startOTAUpdate("THINKNODE_M1_OTA", reply)) {
      if (strlen(reply) > 0) {
        Serial.println(reply);
      }
    } else {
      Serial.println("ERROR: Failed to enter OTA mode");
      bleuart.println("ERROR: Failed to enter OTA mode");
    }
    
  } else if (strcmp(cmd, "help") == 0) {
    bleuart.println("Available commands:");
    bleuart.println("  start ota - Enter OTA firmware update mode");
    bleuart.println("  help      - Show this help");
    bleuart.println("  info      - Show device information");
    bleuart.println("  reboot    - Reboot device");
    
  } else if (strcmp(cmd, "info") == 0) {
    bleuart.printf("Device: %s\r\n", board.getManufacturerName());
    bleuart.printf("Board: ThinkNode M1\r\n");
    bleuart.printf("MCU: nRF52840\r\n");
    bleuart.printf("Firmware: BLE CLI Mode\r\n");
    
    uint16_t batt_mv = board.getBattMilliVolts();
    bleuart.printf("Battery: %d mV\r\n", batt_mv);
    
    // Get MAC address
    ble_gap_addr_t addr;
    if (sd_ble_gap_addr_get(&addr) == NRF_SUCCESS) {
      bleuart.printf("BLE MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
        addr.addr[5], addr.addr[4], addr.addr[3], 
        addr.addr[2], addr.addr[1], addr.addr[0]);
    }
    
  } else if (strcmp(cmd, "reboot") == 0) {
    bleuart.println("Rebooting...");
    bleuart.flush();
    delay(500);
    NVIC_SystemReset();
    
  } else {
    bleuart.printf("Unknown command: %s\r\n", cmd);
    bleuart.println("Type 'help' for available commands");
  }
}
