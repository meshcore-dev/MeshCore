#pragma once

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

// Bounded, clamping printf-append for the fixed-size CLI reply buffers used by
// MQTTBridge's status/stats/diag formatters. Factored out of MQTTBridge so the
// bound is provable on the host instead of holding "by input-size accident"
// (see the A1 out-of-bounds-write finding, 2026-07-19 MQTT observer review).
//
// Appends `fmt...` to `buf` starting at offset `*pos`, then advances `*pos` by
// the number of characters actually written, CLAMPED to [0, bufsize-1]. Because
// snprintf returns the *would-have-written* length, the naive
// `*pos += snprintf(buf + *pos, bufsize - *pos, ...)` idiom can push `*pos` past
// `bufsize` after a truncated append; the next append then computes
// `bufsize - *pos` as a huge size_t and `buf + *pos` past the end, writing out of
// bounds. Clamping `*pos` here makes every subsequent append a safe no-op once
// the buffer is full.
//
// buf is always left NUL-terminated (vsnprintf guarantees this for bufsize > 0).
// No-ops on null buf/pos or bufsize == 0. A negative incoming *pos is treated as
// 0. Typical use: `int pos = 0;` then a sequence of replyAppendf() calls.
static inline void replyAppendf(char* buf, size_t bufsize, int* pos, const char* fmt, ...) {
  if (!buf || !pos || bufsize == 0) return;
  if (*pos < 0) *pos = 0;
  // Full: no room for anything but the terminator. Keep buf NUL-terminated and
  // leave *pos pinned at the last writable index.
  if ((size_t)*pos >= bufsize - 1) {
    *pos = (int)bufsize - 1;
    buf[*pos] = '\0';
    return;
  }
  size_t remaining = bufsize - (size_t)*pos;
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf + *pos, remaining, fmt, args);
  va_end(args);
  // Encoding error: vsnprintf still NUL-terminated buf + *pos; leave *pos as-is.
  if (n < 0) return;
  *pos += n;
  if ((size_t)*pos >= bufsize) *pos = (int)bufsize - 1;  // clamp truncated append
}
