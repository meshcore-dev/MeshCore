#pragma once

#include <stdint.h>

/* Robot-style "bot speak" sound effects for a passive piezo buzzer.

    Each sound is a list of 4-byte segments, small enough to send a whole
    phrase in a single mesh packet:

      f0, f1  start/end frequency, (Hz - 200) / 16, 0 = silence
      dur     duration in 4 ms units
      mod     vibrato: high nibble = rate (x4 Hz), low nibble = depth (x50 Hz)

    Pitch glides exponentially from f0 to f1 over the segment.
*/

struct BotSegment {
  uint8_t f0, f1, dur, mod;
};

#define BOT_F(hz)  ((uint8_t)(((hz) - 200) / 16))
#define BOT_SEG(f0_hz, f1_hz, ms, vib_hz, vib_depth_hz) \
  { BOT_F(f0_hz), BOT_F(f1_hz), (uint8_t)((ms) / 4), (uint8_t)((((vib_hz) / 4) << 4) | ((vib_depth_hz) / 50)) }
#define BOT_REST(ms)  { 0, 0, (uint8_t)((ms) / 4), 0 }

static const BotSegment bot_happy[] = {
  BOT_SEG(1500, 2800,  60,  0,   0),
  BOT_REST(30),
  BOT_SEG(2000, 3500,  50,  0,   0),
  BOT_REST(20),
  BOT_SEG(2500, 4000,  40,  0,   0),
  BOT_SEG(3200, 3200, 120, 32, 300),
};

static const BotSegment bot_sad[] = {
  BOT_SEG(3600, 1300, 700,  8, 100),
  BOT_REST(60),
  BOT_SEG(1500,  900, 400,  8, 100),
};

static const BotSegment bot_question[] = {
  BOT_SEG(1800, 1800, 110,  0,   0),
  BOT_REST(50),
  BOT_SEG(1600, 3400, 280,  0,   0),
};

static const BotSegment bot_excited[] = {
  BOT_SEG(3000, 3600, 160, 48, 250),
  BOT_REST(24),
  BOT_SEG(2600, 4000,  32,  0,   0),
  BOT_SEG(4000, 2800,  32,  0,   0),
  BOT_SEG(2800, 4000,  32,  0,   0),
  BOT_SEG(4000, 2900,  32,  0,   0),
  BOT_SEG(2900, 4100,  32,  0,   0),
  BOT_SEG(4100, 3100,  32,  0,   0),
  BOT_SEG(3100, 4200,  32,  0,   0),
  BOT_REST(24),
  BOT_SEG(3400, 4200,  60,  0,   0),
  BOT_SEG(4200, 4200, 180, 60, 200),
};

static const BotSegment bot_alarm[] = {
  BOT_SEG(3600, 1600, 100, 56, 600),
  BOT_SEG(1600, 3800,  72, 56, 600),
  BOT_REST(20),
  BOT_SEG(3800, 1800,  88, 60, 650),
  BOT_SEG(1800, 4000,  60, 60, 650),
  BOT_REST(16),
  BOT_SEG(4000, 2000,  72, 60, 700),
  BOT_SEG(2000, 4200,  48, 60, 700),
  BOT_SEG(4200, 2200,  64, 60, 750),
  BOT_SEG(2200, 4200,  40, 60, 750),
  BOT_SEG(4200, 2400,  56, 60, 750),
  BOT_SEG(2400, 4200,  36, 60, 750),
  BOT_REST(24),
  BOT_SEG(4200, 1600, 160, 60, 750),
  BOT_REST(32),
  BOT_SEG(4200, 1100, 280, 60, 750),
};

static const BotSegment bot_chatter[] = {
  BOT_SEG(2400, 3100,  56,  0,   0),
  BOT_SEG(3100, 1900,  84,  0,   0),
  BOT_REST(44),
  BOT_SEG(2800, 2800,  72, 40, 300),
  BOT_SEG(1700, 2600,  56,  0,   0),
  BOT_REST(56),
  BOT_SEG(3500, 2200, 100,  0,   0),
  BOT_SEG(2200, 3300,  56,  0,   0),
  BOT_REST(44),
  BOT_SEG(3200, 2600,  72,  0,   0),
  BOT_SEG(2400, 1800, 128,  0,   0),
  BOT_SEG(1800, 1800,  84,  0,   0),
};

static const BotSegment bot_ack[] = {
  BOT_SEG(2700, 2700,  80,  0,   0),
  BOT_REST(40),
  BOT_SEG(1800, 1800, 120,  0,   0),
};

static const BotSegment bot_razz[] = {
  BOT_SEG( 600,  900,  40,  0,   0),
  BOT_SEG( 900,  600, 400, 60, 350),
};

static const BotSegment bot_surprised[] = {
  BOT_SEG(2200, 2200,  32,  0,   0),
  BOT_REST(32),
  BOT_SEG(1500, 4200,  72,  0,   0),
  BOT_SEG(4200, 4200,  40,  0,   0),
};

static const BotSegment bot_hello[] = {
  BOT_SEG(1800, 2600,  60,  0,   0),
  BOT_REST(20),
  BOT_SEG(2600, 3400,  60,  0,   0),
  BOT_REST(20),
  BOT_SEG(3000, 3000,  48,  0,   0),
  BOT_SEG(3400, 4000,  80,  0,   0),
  BOT_SEG(4000, 4000, 100, 40, 200),
};

static const BotSegment bot_goodbye[] = {
  BOT_SEG(2400, 2500,  80,  0,   0),
  BOT_REST(48),
  BOT_SEG(1700, 1400, 200,  0,   0),
};

static const BotSegment bot_uhoh[] = {
  BOT_SEG(2600, 2800, 140,  0,   0),
  BOT_REST(40),
  BOT_SEG(1600, 1100, 220, 48, 200),
};

static const BotSegment bot_low_battery[] = {
  BOT_SEG(2400, 2300, 120,  0,   0),
  BOT_REST(60),
  BOT_SEG(1800, 1500, 200,  0,   0),
  BOT_REST(100),
  BOT_SEG(1500,  700, 600,  4, 100),
};

static const BotSegment bot_yes[] = {
  BOT_SEG(4100, 4100, 100,  0,   0),
  BOT_REST(8),
  BOT_SEG(4100, 4100,  32,  0,   0),
  BOT_REST(12),
  BOT_SEG(4100, 4100,  16,  0,   0),
  BOT_REST(16),
  BOT_SEG(4100, 4100,   8,  0,   0),
};

static const BotSegment bot_no[] = {
  BOT_SEG(1300, 1000, 160, 60, 250),
  BOT_REST(60),
  BOT_SEG(1300, 1000, 160, 60, 250),
};

#define BOT_COUNT(a)  ((uint8_t)(sizeof(a) / sizeof((a)[0])))

/* Words in a chat message that trigger a sound (case-insensitive, whole words). */
struct BotWord {
  const char* word;
  const BotSegment* segs;
  uint8_t count;
};

static const BotWord bot_words[] = {
  { "happy",       bot_happy,       BOT_COUNT(bot_happy) },
  { "sad",         bot_sad,         BOT_COUNT(bot_sad) },
  { "question",    bot_question,    BOT_COUNT(bot_question) },
  { "excited",     bot_excited,     BOT_COUNT(bot_excited) },
  { "alarm",       bot_alarm,       BOT_COUNT(bot_alarm) },
  { "chatter",     bot_chatter,     BOT_COUNT(bot_chatter) },
  { "ack",         bot_ack,         BOT_COUNT(bot_ack) },
  { "razz",        bot_razz,        BOT_COUNT(bot_razz) },
  { "surprised",   bot_surprised,   BOT_COUNT(bot_surprised) },
  { "hello",       bot_hello,       BOT_COUNT(bot_hello) },
  { "goodbye",     bot_goodbye,     BOT_COUNT(bot_goodbye) },
  { "uh-oh",       bot_uhoh,        BOT_COUNT(bot_uhoh) },
  { "uh oh",       bot_uhoh,        BOT_COUNT(bot_uhoh) },
  { "uhoh",        bot_uhoh,        BOT_COUNT(bot_uhoh) },
  { "low battery", bot_low_battery, BOT_COUNT(bot_low_battery) },
  { "yes",         bot_yes,           BOT_COUNT(bot_yes) },
  { "y",           bot_yes,           BOT_COUNT(bot_yes) },
  { "no",          bot_no,            BOT_COUNT(bot_no) },
  { "n",           bot_no,            BOT_COUNT(bot_no) },
};
