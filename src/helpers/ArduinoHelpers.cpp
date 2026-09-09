#include <stdint.h>

extern "C" {
    __attribute__((section(".persistent_magic"))) uint32_t persistent_magic;
    __attribute__((section(".persistent_data"))) uint32_t persistent_time;
}