#ifdef ASR650X_PLATFORM
// Linked before libc: these definitions replace newlib's strtod/strtof/atof (and the ~7 KB of gdtoa they pull in).
#include "SmallStrtod.h"

extern "C" {
double strtod(const char* s, char** end) { return asr650x_strtod(s, end); }
float strtof(const char* s, char** end) { return (float)asr650x_strtod(s, end); }
double atof(const char* s) { return asr650x_strtod(s, 0); }
}
#endif
