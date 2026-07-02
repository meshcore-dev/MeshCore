/*
 * Zephyr backing for MeshCore's nrf54 InternalFileSystem (the header MeshCore's
 * NRF54_PLATFORM branch includes). Same Adafruit_LittleFS class, lfs block device
 * over Zephyr flash_area on `storage_partition` (validated in zephyr-port/06_fs).
 * Compile THIS instead of the bare-metal RRAMC InternalFileSystem.cpp.
 */
#include <helpers/nrf54/InternalFileSystem.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#define FS_PARTITION  storage_partition

#define BS            2048            /* littlefs logical block (== read/prog size) */
#define BC            12              /* 24 KB storage_partition / 2048 */

static const struct flash_area *g_fa = NULL;

static int _fa_read(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, void *buf, lfs_size_t sz) {
	(void)c; return flash_area_read(g_fa, (off_t)b * BS + o, buf, sz) == 0 ? LFS_ERR_OK : LFS_ERR_IO;
}
static int _fa_prog(const struct lfs_config *c, lfs_block_t b, lfs_off_t o, const void *buf, lfs_size_t sz) {
	(void)c; return flash_area_write(g_fa, (off_t)b * BS + o, buf, sz) == 0 ? LFS_ERR_OK : LFS_ERR_IO;
}
static int _fa_erase(const struct lfs_config *c, lfs_block_t b) {
	(void)c; (void)b;

	return LFS_ERR_OK;
}
static int _fa_sync(const struct lfs_config *c) { (void)c; return LFS_ERR_OK; }

struct lfs_config _InternalFSConfig = {
	.context = NULL,
	.read  = _fa_read, .prog = _fa_prog, .erase = _fa_erase, .sync = _fa_sync,
	.read_size = BS, .prog_size = BS, .block_size = BS, .block_count = BC, .lookahead = 128,
	.read_buffer = NULL, .prog_buffer = NULL, .lookahead_buffer = NULL, .file_buffer = NULL
};

InternalFileSystem InternalFS;
InternalFileSystem::InternalFileSystem(void) : Adafruit_LittleFS(&_InternalFSConfig) {}

bool InternalFileSystem::begin(void) {
	if (g_fa == NULL && flash_area_open(FIXED_PARTITION_ID(FS_PARTITION), &g_fa) != 0) {
		printk("FS: flash_area_open FAILED\n");
		return false;
	}
	if (!Adafruit_LittleFS::begin()) {
		printk("FS: mount FAILED -> reformatting (any persisted prefs/contacts are lost)\n");
		this->format();
		if (!Adafruit_LittleFS::begin()) { printk("FS: format+remount FAILED\n"); return false; }
	} else {
		printk("FS: mounted OK (persisted data intact)\n");
	}
	return true;
}
