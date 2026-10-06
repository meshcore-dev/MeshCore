#include "BigStack.h"
#include <stdlib.h>
#include <string.h>

static uint8_t* g_stack = 0;
static size_t g_size = 0;

bool asr650x_bigstack_init(size_t bytes) {
  if (g_stack) return true;
  g_stack = (uint8_t*)malloc(bytes);
  if (!g_stack) return false;
  g_size = bytes;
  memset(g_stack, 0xA5, bytes);
  return true;
}

bool asr650x_bigstack_ready() { return g_stack != 0; }

size_t asr650x_bigstack_highwater() {
  if (!g_stack) return 0;
  size_t i = 0;
  while (i < g_size && g_stack[i] == 0xA5) i++;
  return g_size - i;
}

void asr650x_call_on_big_stack(void (*fn)(void*), void* ctx) {
  if (!g_stack) { fn(ctx); return; }              // not initialised: stay on the current stack
  uint32_t top = ((uint32_t)(g_stack + g_size)) & ~7u;   // 8-byte aligned top of stack
  register void* r0 asm("r0") = ctx;
  register void (*r1)(void*) asm("r1") = fn;
  register uint32_t r2 asm("r2") = top;
  asm volatile(
    "mov r4, sp\n\t"
    "mov sp, r2\n\t"
    "blx r1\n\t"
    "mov sp, r4\n\t"
    : "+r"(r0), "+r"(r1), "+r"(r2)
    :
    : "r3", "r4", "r12", "lr", "memory", "cc");
}
