#pragma once

namespace rtttl {
inline bool playing = false;
inline int starts = 0;
inline int stops = 0;

inline void begin(int, const char*) { playing = true; starts++; }
inline bool isPlaying() { return playing; }
inline bool done() { return !playing; }
inline void stop() { playing = false; stops++; }
inline void play() { }
}
