#pragma once

#include <Arduino.h>
#include <bluefruit.h>

#ifndef BLE_CLI_TX_POWER
#define BLE_CLI_TX_POWER 4
#endif

#define BLE_CLI_BUFFER_SIZE 256

/**
 * BLE CLI Interface - Provides command-line access over Bluetooth
 * This runs as a separate BLE UART service alongside the companion protocol
 * Allows users to issue commands like "start ota" wirelessly
 */
class BLECLIInterface {
private:
  BLEUart cli_uart;  // Separate UART service for CLI
  char input_buffer[BLE_CLI_BUFFER_SIZE];
  uint16_t input_pos;
  bool enabled;
  bool connected;
  uint16_t conn_handle;
  
  void (*command_callback)(const char* command, char* reply, size_t reply_size);
  
  static BLECLIInterface* instance;
  static void onConnect(uint16_t connection_handle);
  static void onDisconnect(uint16_t connection_handle, uint8_t reason);
  static void onRxCallback(uint16_t conn_handle);
  
  void processChar(char c);
  void processCommand();
  void sendPrompt();

public:
  BLECLIInterface();
  
  /**
   * Initialize BLE CLI interface
   * @param device_name Name for the BLE device (e.g. "THINKNODE_M1_CLI")
   * @param callback Function to call when command is received
   */
  void begin(const char* device_name, void (*callback)(const char* cmd, char* reply, size_t reply_size));
  
  /**
   * Update loop - call regularly from main loop
   */
  void update();
  
  /**
   * Check if BLE CLI is connected
   */
  bool isConnected() const { return connected; }
  
  /**
   * Print text to BLE CLI
   */
  void print(const char* text);
  void println(const char* text);
  void printf(const char* format, ...);
};
