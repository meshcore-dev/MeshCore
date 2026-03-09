#include "BLECLIInterface.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

BLECLIInterface* BLECLIInterface::instance = nullptr;

BLECLIInterface::BLECLIInterface() {
  enabled = false;
  connected = false;
  conn_handle = BLE_CONN_HANDLE_INVALID;
  input_pos = 0;
  command_callback = nullptr;
  memset(input_buffer, 0, sizeof(input_buffer));
}

void BLECLIInterface::begin(const char* device_name, void (*callback)(const char* cmd, char* reply, size_t reply_size)) {
  instance = this;
  command_callback = callback;
  
  // Note: Bluefruit.begin() should already be called by SerialBLEInterface
  // We only need to add our CLI UART service
  
  // Start BLE UART service for CLI
  cli_uart.begin();
  cli_uart.setRxCallback(onRxCallback);
  
  // Set connection callbacks
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);
  
  // Note: Don't call Bluefruit.begin() here - it's already initialized
  // Just add our service to the existing BLE setup
  
  enabled = true;
  
  Serial.println("BLE CLI Interface initialized");
  Serial.printf("  Device name: %s\r\n", device_name);
  Serial.println("  Connect via BLE UART to access CLI");
}

void BLECLIInterface::onConnect(uint16_t connection_handle) {
  Serial.printf("BLE CLI: Connected (handle 0x%04X)\r\n", connection_handle);
  if (instance) {
    instance->conn_handle = connection_handle;
    instance->connected = true;
    instance->input_pos = 0;
    instance->sendPrompt();
  }
}

void BLECLIInterface::onDisconnect(uint16_t connection_handle, uint8_t reason) {
  Serial.printf("BLE CLI: Disconnected (handle 0x%04X, reason %u)\r\n", connection_handle, reason);
  if (instance && instance->conn_handle == connection_handle) {
    instance->conn_handle = BLE_CONN_HANDLE_INVALID;
    instance->connected = false;
    instance->input_pos = 0;
  }
}

void BLECLIInterface::onRxCallback(uint16_t conn_handle) {
  if (!instance || !instance->enabled) return;
  
  // Read all available characters
  while (instance->cli_uart.available()) {
    char c = instance->cli_uart.read();
    instance->processChar(c);
  }
}

void BLECLIInterface::processChar(char c) {
  // Handle backspace
  if (c == '\b' || c == 0x7F) {
    if (input_pos > 0) {
      input_pos--;
      cli_uart.write("\b \b");  // Erase character on terminal
    }
    return;
  }
  
  // Echo character (except special chars)
  if (c >= 32 && c < 127) {
    cli_uart.write(c);
  }
  
  // Handle Enter/Return
  if (c == '\r' || c == '\n') {
    cli_uart.write("\r\n");
    
    if (input_pos > 0) {
      input_buffer[input_pos] = '\0';
      processCommand();
      input_pos = 0;
    }
    
    sendPrompt();
    return;
  }
  
  // Add to buffer if there's space
  if (input_pos < BLE_CLI_BUFFER_SIZE - 1 && c >= 32 && c < 127) {
    input_buffer[input_pos++] = c;
  }
}

void BLECLIInterface::processCommand() {
  if (!command_callback) {
    println("ERROR: No command handler registered");
    return;
  }
  
  // Prepare reply buffer
  char reply[256];
  reply[0] = '\0';
  
  // Call the command handler
  command_callback(input_buffer, reply, sizeof(reply));
  
  // Send reply if any
  if (strlen(reply) > 0) {
    println(reply);
  }
}

void BLECLIInterface::sendPrompt() {
  if (connected) {
    cli_uart.write("> ");
  }
}

void BLECLIInterface::update() {
  // Nothing special needed here - callbacks handle everything
}

void BLECLIInterface::print(const char* text) {
  if (connected && text) {
    cli_uart.write(text, strlen(text));
  }
}

void BLECLIInterface::println(const char* text) {
  if (connected) {
    if (text) {
      cli_uart.write(text, strlen(text));
    }
    cli_uart.write("\r\n");
  }
}

void BLECLIInterface::printf(const char* format, ...) {
  if (!connected) return;
  
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  
  cli_uart.write(buffer, strlen(buffer));
}
