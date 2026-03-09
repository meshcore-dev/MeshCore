#include <Arduino.h>
#include "ThinkNodeM1Board.h"
#include <helpers/nrf52/BLECLIInterface.h>
#include <Mesh.h>
#include <helpers/CommonCLI.h>

// Forward declarations
extern mesh::Mesh* getMesh();
extern mesh::MainBoard* getBoard();

// BLE CLI instance
BLECLIInterface ble_cli;

// Command handler for BLE CLI
void handleBLECLICommand(const char* command, char* reply, size_t reply_size) {
  mesh::Mesh* mesh = getMesh();
  mesh::MainBoard* board = getBoard();
  
  if (!mesh || !board) {
    snprintf(reply, reply_size, "ERROR: System not initialized");
    return;
  }
  
  // Create a CommonCLI instance to handle the command
  // Note: This requires access to the mesh and its components
  // For now, we'll handle the most important commands directly
  
  if (strncmp(command, "start ota", 9) == 0) {
    if (board->startOTAUpdate("THINKNODE_M1_OTA", reply)) {
      // Success - reply already set
      if (strlen(reply) == 0) {
        snprintf(reply, reply_size, "OK - Entering OTA mode");
      }
    } else {
      snprintf(reply, reply_size, "ERROR: Failed to enter OTA mode");
    }
  } else if (strncmp(command, "help", 4) == 0) {
    snprintf(reply, reply_size, "Available commands:\r\n  start ota - Enter OTA firmware update mode\r\n  help - Show this help\r\n  version - Show firmware version");
  } else if (strncmp(command, "version", 7) == 0) {
    snprintf(reply, reply_size, "MeshCore ThinkNode M1 Companion (BLE+CLI)");
  } else {
    snprintf(reply, reply_size, "Unknown command: %s\r\nType 'help' for available commands", command);
  }
}

// Initialize BLE CLI
void initBLECLI() {
  // Wait a bit for BLE to be fully initialized by SerialBLEInterface
  delay(1000);
  
  ble_cli.begin("THINKNODE_M1_CLI", handleBLECLICommand);
  
  Serial.println("");
  Serial.println("=========================================");
  Serial.println("ThinkNode M1 - BLE CLI Enabled");
  Serial.println("=========================================");
  Serial.println("Connect via BLE UART to access CLI");
  Serial.println("Available commands:");
  Serial.println("  start ota - Enter OTA mode");
  Serial.println("  help      - Show help");
  Serial.println("  version   - Show version");
  Serial.println("=========================================");
}

// Update BLE CLI
void updateBLECLI() {
  ble_cli.update();
}
