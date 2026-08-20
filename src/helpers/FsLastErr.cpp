#include "FsLastErr.h"
#include <stdio.h>
#include <string.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <Adafruit_LittleFS.h>
  #include "littlefs/lfs.h"
  #ifndef LFS_ERR_NOSPC
    #define LFS_ERR_NOSPC (-28)
  #endif
#endif

static int s_last_lfs_err = 0;

void fsLastErrClear() {
  s_last_lfs_err = 0;
}

void fsLastErrSet(int err) {
  if (err != 0) s_last_lfs_err = err;
}

int fsLastErrGet() {
  return s_last_lfs_err;
}

static bool fsErrIsNospc(int err) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return err == LFS_ERR_NOSPC;
#else
  (void) err;
  return false;
#endif
}

void fsLastErrStage(char* stage, size_t stage_len, int err, const char* fallback_stage) {
  if (!stage || stage_len == 0) return;
  if (fsErrIsNospc(err)) {
    strncpy(stage, "nospc", stage_len - 1);
  } else if (fallback_stage && fallback_stage[0]) {
    strncpy(stage, fallback_stage, stage_len - 1);
  } else {
    strncpy(stage, "write", stage_len - 1);
  }
  stage[stage_len - 1] = 0;
}

void fsLastErrReply(char* reply, size_t reply_len, int err, const char* fallback_stage) {
  if (!reply || reply_len == 0) return;

  const char* stage = (fallback_stage && fallback_stage[0]) ? fallback_stage : "write";

  if (fsErrIsNospc(err) || strcmp(stage, "nospc") == 0) {
    snprintf(reply, reply_len, "ERR no space left on device (try: doctor gc)");
    return;
  }

  if (strcmp(stage, "serialize") == 0) {
    if (err != 0) {
      snprintf(reply, reply_len, "ERR prefs serialize failed lfs=%d (try: doctor gc)", err);
    } else {
      snprintf(reply, reply_len, "ERR prefs serialize failed (try: doctor gc)");
    }
    return;
  }

  if (err != 0) {
    snprintf(reply, reply_len, "ERR prefs %s failed lfs=%d (try: doctor gc)", stage, err);
    return;
  }

  snprintf(reply, reply_len, "ERR prefs %s failed (try: doctor gc)", stage);
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)

static int fsCountBlock(void* p, lfs_block_t block) {
  (void) block;
  lfs_size_t* count = (lfs_size_t*) p;
  (*count)++;
  return 0;
}

bool fsIsCriticallyFull(FILESYSTEM* fs) {
  if (!fs) return false;
  lfs_t* lfs = fs->_getFS();
  if (!lfs || !lfs->cfg) return false;
  lfs_size_t used = 0;
  if (lfs_traverse(lfs, fsCountBlock, &used) != 0) return false;
  return used + 2 >= lfs->cfg->block_count;
}

#endif

void fsLastErrReplyForFs(char* reply, size_t reply_len, int err, const char* stage, FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (err == 0 && fs && fsIsCriticallyFull(fs)) {
    snprintf(reply, reply_len, "ERR no space left on device (try: doctor gc)");
    return;
  }
#else
  (void) fs;
#endif
  fsLastErrReply(reply, reply_len, err, stage);
}
