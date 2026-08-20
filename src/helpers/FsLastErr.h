#pragma once

#include <stddef.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM) || defined(ESP32) || defined(RP2040_PLATFORM)
  #include "IdentityStore.h"
#endif

void fsLastErrClear();
void fsLastErrSet(int err);
int fsLastErrGet();

void fsLastErrStage(char* stage, size_t stage_len, int err, const char* fallback_stage);

void fsLastErrReply(char* reply, size_t reply_len, int err, const char* fallback_stage);

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
bool fsIsCriticallyFull(FILESYSTEM* fs);
#endif

void fsLastErrReplyForFs(char* reply, size_t reply_len, int err, const char* stage, FILESYSTEM* fs);
