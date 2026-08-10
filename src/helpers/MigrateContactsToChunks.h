#pragma once

#include <Adafruit_LittleFS.h>

// called from MigrateTo4K
void migrateContactsFromBuffer(Adafruit_LittleFS& fs, const uint8_t* data, uint32_t dataSize);

// called from loadContacts
bool migrateContactsFromFile(Adafruit_LittleFS& fs, uint8_t* chunkBuf);
