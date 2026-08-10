#include "MigrateTo4K.h"
#include <helpers/ContactInfo.h>
#include <helpers/MigrateContactsToChunks.h>

struct BufferedFile {
    uint8_t* data;
    uint32_t size;
};

static BufferedFile bufferFile(Adafruit_LittleFS& fs, const char* path)
{
    BufferedFile buf = { nullptr, 0 };
    File f = fs.open(path, FILE_O_READ);
    if (!f) return buf;

    uint32_t size = f.size();
    if (size > 0) {
        buf.data = (uint8_t*)malloc(size);
        if (buf.data) {
            f.read(buf.data, size);
            buf.size = size;
            MESH_DEBUG_PRINTLN("MigrateTo4K: buffered %s (%d bytes)", path, size);
        } else {
            MESH_DEBUG_PRINTLN("MigrateTo4K: malloc failed buffering %s (%d bytes)", path, size);
        }
    }
    f.close();
    return buf;
}

static bool migrateFS(CustomLFS2& fs, uint32_t oldBlockSize) {
    lfs_config migrationConfig = {};
    migrationConfig.read_size      = 32;
    migrationConfig.prog_size      = 32;
    migrationConfig.cache_size     = 32;
    migrationConfig.lookahead_size = 32;
    migrationConfig.block_cycles   = 512;
    // check if already on 4k blocks
    fs.setFlashRegion(fs.getFlashAddr(), fs.getFlashSize(), 4096);
    if (fs.begin(false)) {
        MESH_DEBUG_PRINTLN("MigrateTo4K: already on 4kb block, skipping migration");
        return true;
    }

    // mount with old block size and let begin() migrate LFSv1 to LFSv2
    fs.setFlashRegion(fs.getFlashAddr(), fs.getFlashSize(), oldBlockSize, migrationConfig);
    if (!fs.begin(false)) {
        MESH_DEBUG_PRINTLN("MigrateTo4K: mount with %d-byte blocks failed, formatting fresh", oldBlockSize);
        fs.setFlashRegion(fs.getFlashAddr(), fs.getFlashSize(), 4096);
        fs.formatRegion();
        return fs.begin(false);
    }

    MESH_DEBUG_PRINTLN("MigrateTo4K: mounted with %d-byte blocks", oldBlockSize);
    return true;
}

static bool reformatTo4K(CustomLFS2& fs)
{
    lfs_config migrationConfig = {};
    migrationConfig.read_size      = 32;
    migrationConfig.prog_size      = 32;
    migrationConfig.cache_size     = 32;
    migrationConfig.lookahead_size = 32;
    migrationConfig.block_cycles   = 512;

    fs.end();
    fs.setFlashRegion(fs.getFlashAddr(), fs.getFlashSize(), 4096);
    fs.formatRegion();

    if (!fs.begin(false)) {
        MESH_DEBUG_PRINTLN("MigrateTo4K: ERROR: failed to mount after formatting with 4k blocks");
        return false;
    }
    return true;
}


void migrateTo4kBlocks(CustomLFS2& secondaryFS, uint32_t oldBlockSize) {
    // this config uses less ram so that we have room to buffer files for migration to 4k blocks
    lfs_config migrationConfig = {};
    migrationConfig.read_size      = 32;
    migrationConfig.prog_size      = 32;
    migrationConfig.cache_size     = 32;
    migrationConfig.lookahead_size = 32;
    migrationConfig.block_cycles   = 512;

    if (migrateFS(secondaryFS, oldBlockSize)) {
        if (secondaryFS.getBlockSize() != 4096) {
            BufferedFile contacts = bufferFile(secondaryFS, "/contacts3");
            BufferedFile channels = bufferFile(secondaryFS, "/channels2");

            if (reformatTo4K(secondaryFS)) {
                if (contacts.data && contacts.size > 0) {
                    migrateContactsFromBuffer(secondaryFS, contacts.data, contacts.size);
                }

                if (channels.data && channels.size > 0) {
                    File f = secondaryFS.open("/channels2", FILE_O_WRITE);
                    if (f) {
                        f.write(channels.data, channels.size);
                        f.close();
                        MESH_DEBUG_PRINTLN("MigrateTo4K: restored /channels2 (%d bytes)", channels.size);
                    }
                }

                MESH_DEBUG_PRINTLN("MigrateTo4K: migration complete");
            }
            
            free(contacts.data);
            free(channels.data);
        }
        secondaryFS.end();
    }
}