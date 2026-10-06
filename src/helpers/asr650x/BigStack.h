#pragma once
#include <stddef.h>
#include <stdint.h>

// Runs memory-hungry crypto on a separate stack taken from the (mostly empty) heap, because the
// linker script fixes the main stack at 2 KB (M0 report, R9).
bool asr650x_bigstack_init(size_t bytes);
bool asr650x_bigstack_ready();                       // false if the heap could not provide the stack
size_t asr650x_bigstack_highwater();                 // bytes of the big stack ever used
void asr650x_call_on_big_stack(void (*fn)(void*), void* ctx);
