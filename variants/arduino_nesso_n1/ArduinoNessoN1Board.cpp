#include "ArduinoNessoN1Board.h"
#include <Arduino.h>

void ArduinoNessoN1Board::begin() {
  ESP32Board::begin();

#ifdef MESH_DEBUG
  // delay for 2s after boot to ensure early output below makes it to the serial logger
  delay(2000);
#endif

#ifdef P_LORA_TX_LED
  MESH_DEBUG_PRINTLN("ArduinoNessoN1.begin(): setup TX LED mode");
  pinMode(P_LORA_TX_LED, OUTPUT);
  digitalWrite(P_LORA_TX_LED, HIGH);
#endif

  battery.begin();
  battery.enableCharge();

  MESH_DEBUG_PRINTLN("ArduinoNessoN1.begin(): set Nesso N1 pin modes and default states...");
  pinMode(LORA_ENABLE, OUTPUT);         // RESET
  pinMode(LORA_ANTENNA_SWITCH, OUTPUT); // ANTENNA_SWITCH
  pinMode(LORA_LNA_ENABLE, OUTPUT);     // LNA_ENABLE
  pinMode(LCD_BACKLIGHT, OUTPUT);
  pinMode(BEEP_PIN, OUTPUT);

  // Toggle LoRa reset via expander
  MESH_DEBUG_PRINTLN("ArduinoNessoN1.begin(): Enable LoRa...");
  digitalWrite(LORA_ENABLE, LOW);
  delay(10);
  digitalWrite(LORA_ENABLE, HIGH);

  // Configure antenna switch and LNA
  digitalWrite(LORA_ANTENNA_SWITCH, HIGH); // enable antenna switch
  digitalWrite(LORA_LNA_ENABLE, HIGH);     // enable LNA

  // Configure initial state of further devices on expander
  MESH_DEBUG_PRINTLN("ArduinoNessoN1.begin(): Set LCD_BACKLIGHT and BEEP_PIN to low initial state...");
  digitalWrite(LCD_BACKLIGHT, LOW);
  digitalWrite(BEEP_PIN, LOW);

  // Toggle LCD backlight to show the device has powered on until we get the screen working
  MESH_DEBUG_PRINTLN("ArduinoNessoN1.begin(): Now high...");
  digitalWrite(LCD_BACKLIGHT, HIGH);
  digitalWrite(BEEP_PIN, HIGH);
  delay(2000);
  digitalWrite(LCD_BACKLIGHT, LOW);
  digitalWrite(BEEP_PIN, LOW);
  MESH_DEBUG_PRINTLN("ArduinoNessoN1.begin(): Now low...");
}