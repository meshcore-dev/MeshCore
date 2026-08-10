#include <helpers/MigrateContactsToChunks.h>
#include <helpers/ContactInfo.h>
#include <Adafruit_LittleFS.h>
using namespace Adafruit_LittleFS_Namespace;

static void writeContactChunk(Adafruit_LittleFS& fs, uint8_t* chunkBuf, uint8_t chunkIndex, uint8_t count) {
    const uint32_t chunkSize = sizeof(ChunkHeader) + (SLOTS_PER_CHUNK * CONTACT_RECORD_SIZE);
    const uint32_t usedBytes = (uint32_t)count * CONTACT_RECORD_SIZE;

    memset(chunkBuf + sizeof(ChunkHeader) + usedBytes, 0,
           (uint32_t)(SLOTS_PER_CHUNK - count) * CONTACT_RECORD_SIZE);

    ChunkHeader* header = (ChunkHeader*)chunkBuf;
    header->magic       = CHUNK_MAGIC;
    header->version     = CHUNK_VERSION;
    header->chunk_index = chunkIndex;
    header->valid_count = count;
    memset(header->tombstones, 1, SLOTS_PER_CHUNK);
    for (uint8_t i = 0; i < count; i++) header->tombstones[i] = 0;
    memset(header->reserved, 0, sizeof(header->reserved));
    memset(header->sha256, 0, 32);

    mesh::Utils::sha256(header->sha256, 32, chunkBuf, chunkSize);

    char filename[20];
    snprintf(filename, sizeof(filename), "/contacts3_%02d", chunkIndex);
    File f = fs.open(filename, FILE_O_WRITE);
    if (f) { f.write(chunkBuf, chunkSize); f.close(); }
}

void migrateContactsFromBuffer(Adafruit_LittleFS& fs, const uint8_t* data, uint32_t dataSize) {
    const uint32_t chunkSize = sizeof(ChunkHeader) + (SLOTS_PER_CHUNK * CONTACT_RECORD_SIZE);
    uint32_t numContacts = dataSize / CONTACT_RECORD_SIZE;
    uint32_t numChunks   = (numContacts + SLOTS_PER_CHUNK - 1) / SLOTS_PER_CHUNK;

    MESH_DEBUG_PRINTLN("MigrateContactsToChunks: writing %d contacts as %d chunks", numContacts, numChunks);

    uint8_t* chunkBuf = (uint8_t*)malloc(chunkSize);
    if (!chunkBuf) {
        MESH_DEBUG_PRINTLN("MigrateContactsToChunks: couldn't malloc chunk buffer (%d bytes)", chunkSize);
        return;
    }

    for (uint8_t chunk = 0; chunk < numChunks && chunk < MAX_CHUNKS; chunk++) {
        uint8_t count = (chunk < numChunks - 1)
            ? SLOTS_PER_CHUNK
            : (numContacts - chunk * SLOTS_PER_CHUNK);

        memcpy(chunkBuf + sizeof(ChunkHeader),
               data + ((uint32_t)chunk * SLOTS_PER_CHUNK * CONTACT_RECORD_SIZE),
               (uint32_t)count * CONTACT_RECORD_SIZE);

        writeContactChunk(fs, chunkBuf, chunk, count);
    }

    free(chunkBuf);
}

bool migrateContactsFromFile(Adafruit_LittleFS& fs, uint8_t* chunkBuf) {
    File in = fs.open("/contacts3", FILE_O_READ);
    if (!in) return false;

    uint32_t numContacts = in.size() / CONTACT_RECORD_SIZE;
    MESH_DEBUG_PRINTLN("MigrateContactsToChunks: migrating monolithic /contacts3 (%d contacts)", numContacts);

    uint8_t  chunk_idx = 0;
    uint32_t remaining = numContacts;

    while (remaining > 0 && chunk_idx < MAX_CHUNKS) {
        uint8_t  count = (remaining > SLOTS_PER_CHUNK) ? SLOTS_PER_CHUNK : (uint8_t)remaining;
        uint32_t want  = (uint32_t)count * CONTACT_RECORD_SIZE;

        if ((uint32_t)in.read(chunkBuf + sizeof(ChunkHeader), want) != want) {
            MESH_DEBUG_PRINTLN("MigrateContactsToChunks: read failed at chunk %d", chunk_idx);
            in.close();
            return false;
        }

        writeContactChunk(fs, chunkBuf, chunk_idx, count);   // fs is a reference now, no *

        remaining -= count;
        chunk_idx++;
    }
    in.close();
    return (remaining == 0);
}
