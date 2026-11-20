// Override of the core's weak _sbrk (Cm0plusStart.c). The core caps the heap at CYDEV_HEAP_SIZE (4096 B)
// although the linker leaves everything between .bss and the 2 KB stack unused. The crypto stack (2 KB), the
// packet pool, the UART buffer and the flash page cache (256 B, allocated during every SFLASH write) need
// more than 4 KB, so with the core's version malloc() fails. Here the heap may grow up to the bottom of the
// stack.
#include <errno.h>
#include <stdint.h>

extern "C" {
extern int end;                     // linker: first free byte after .bss
extern uint32_t __cy_stack_limit[]; // linker: bottom of the 2 KB stack region

void *_sbrk(int nbytes) {
  static uint8_t *heap_ptr = (uint8_t *)&end;
  uint8_t *limit = (uint8_t *)__cy_stack_limit;
  uint8_t *next = heap_ptr + nbytes;
  if (next > limit || next < (uint8_t *)&end) {
    errno = ENOMEM;
    return (void *)-1;
  }
  void *prev = heap_ptr;
  heap_ptr = next;
  return prev;
}
}
