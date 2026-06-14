#pragma once

#include "FILESYSTEM.h"

// called from MigrateTo4K
void migrateContactsFromBuffer(FILESYSTEM* fs, const uint8_t* data, uint32_t dataSize);

// called from loadContacts
bool migrateContactsFromFile(FILESYSTEM* fs, uint8_t* chunkBuf);
