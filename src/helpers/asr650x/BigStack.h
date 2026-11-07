#pragma once
#include <stddef.h>
#include <stdint.h>

// Runs memory-hungry crypto on a separate stack taken from the (mostly empty) heap, because the
// linker script fixes the main stack at 2 KB, and Ed25519 needs ~1.8 KB on its own.
bool asr650xBigstackInit(size_t bytes);
bool asr650xBigstackReady();       // false if the heap could not provide the stack
size_t asr650xBigstackHighwater(); // bytes of the big stack ever used
void asr650xCallOnBigStack(void (*fn)(void *), void *ctx);
