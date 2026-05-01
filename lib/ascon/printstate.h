#ifndef PRINTSTATE_H_
#define PRINTSTATE_H_

#ifdef ASCON_PRINT_STATE

#include "ascon.h"

void print(const char* text);
void printbytes(const char* text, const uint8_t* b, uint64_t len);
void printword(const char* text, const uint64_t x);
void printstate(const char* text, const ascon_state_t* s);

#else

#define print(text) \
  do {              \
  } while (0)

#define printbytes(text, b, l) \
  do {                         \
  } while (0)

#define printword(text, w) \
  do {                     \
  } while (0)

#define printstate(text, s) \
  do {                      \
  } while (0)

#endif

#endif /* PRINTSTATE_H_ */
