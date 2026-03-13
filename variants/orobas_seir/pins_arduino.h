#pragma once

#include <stdint.h>

// =====================================================================
// Arduino core required pins for OROBAS SEIR v5 Board
// =====================================================================

// Pin 1 is used for LORA TX
#ifndef LED_BUILTIN
#define LED_BUILTIN 1
#endif

// LORA SCK (Serial Clock)
#ifndef SCK
#define SCK 10
#endif

// LORA MISO (Master In Slave Out)
#ifndef MISO
#define MISO 9
#endif

// LORA MOSI (Master Out Slave In)
#ifndef MOSI
#define MOSI 11
#endif

// LORA SS (Slave Select)
#ifndef SS
#define SS 6
#endif

// ---------------------------------------------------------------------
// I2C pins (not used on this board, but required by Arduino core)
// These dummy values are defined to satisfy Arduino core requirements
// but I2C is not implemented on this board
// ---------------------------------------------------------------------
#ifndef SDA
#define SDA 255
#endif

#ifndef SCL
#define SCL 255
#endif

// ---------------------------------------------------------------------
// UART pins (GPS communication)
// These pins are used specifically for GPS module communication
// ---------------------------------------------------------------------
#ifndef TX
#define TX 7 // GPS TX (Transmit) pin - connected to GPS module's RX pin
#endif

#ifndef RX
#define RX 5 // GPS RX (Receive) pin - connected to GPS module's TX pin
#endif
