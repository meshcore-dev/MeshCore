#include "Arduino.h"
#ifdef PIN_BUZZER
#include "buzzer.h"
#include "botspeak_sounds.h"

#define BOT_TICK_MILLIS  2    // how often the sweep frequency is updated
#define BOT_GAP_MILLIS   150  // pause between queued sounds

void genericBuzzer::begin() {
//    Serial.print("DBG: Setting up buzzer on pin ");
//    Serial.println(PIN_BUZZER);
    #ifdef PIN_BUZZER_EN
      pinMode(PIN_BUZZER_EN, OUTPUT);
      digitalWrite(PIN_BUZZER_EN, HIGH);
    #endif

    quiet(false);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); // need to pull low by default to avoid extreme power draw
}

void genericBuzzer::play(const char *melody) {
    if (isPlaying())   // interrupt existing
    {
        rtttl::stop();
        botStop();
    }

    if (_is_quiet) return;

    rtttl::begin(PIN_BUZZER,melody);
//    Serial.print("DBG: Playing melody - isQuiet: ");
//    Serial.println(isQuiet());
}

bool genericBuzzer::isPlaying() {
    return rtttl::isPlaying() || _bot_queued > 0;
}

void genericBuzzer::loop() {
    if (!rtttl::done()) rtttl::play();
    if (_bot_queued > 0) botLoop();
}

void genericBuzzer::playBot(const BotSegment *segs, uint8_t count) {
    BotSound sound = { segs, count };
    botStart(&sound, 1);
}

// match a bot speak word at p (case-insensitive, must end at a word boundary), returns its length or 0
static int matchBotWord(const char *p, const BotWord **found) {
    for (size_t i = 0; i < sizeof(bot_words) / sizeof(bot_words[0]); i++) {
        const char *w = bot_words[i].word;
        int k = 0;
        while (w[k] && tolower((unsigned char)p[k]) == w[k]) k++;
        if (w[k] == 0 && !isalnum((unsigned char)p[k])) {
            *found = &bot_words[i];
            return k;
        }
    }
    return 0;
}

int genericBuzzer::playWords(const char *text) {
    if (_is_quiet) return 0;

    BotSound sounds[BOT_QUEUE_SIZE];
    int n = 0;
    const char *p = text;
    while (*p && n < BOT_QUEUE_SIZE) {
        const BotWord *w;
        int len = 0;
        if (p == text || !isalnum((unsigned char)p[-1])) len = matchBotWord(p, &w);
        if (len > 0) {
            sounds[n].segs = w->segs;
            sounds[n].count = w->count;
            n++;
            p += len;
        } else {
            p++;
        }
    }
    if (n > 0) botStart(sounds, n);
    return n;
}

void genericBuzzer::botStart(const BotSound *sounds, int count) {
    if (rtttl::isPlaying()) rtttl::stop();
    botStop();
    if (_is_quiet) return;

    for (int i = 0; i < count && i < BOT_QUEUE_SIZE; i++) _bot_queue[i] = sounds[i];
    _bot_queued = count < BOT_QUEUE_SIZE ? count : BOT_QUEUE_SIZE;
    _bot_seg = 0;
    _bot_seg_start = millis();
    _bot_last_tick = _bot_seg_start - BOT_TICK_MILLIS;  // update tone on first loop
}

void genericBuzzer::botStop() {
    _bot_queued = 0;
    if (_bot_tone_on) {
        noTone(PIN_BUZZER);
        digitalWrite(PIN_BUZZER, LOW);
        _bot_tone_on = false;
    }
}

void genericBuzzer::botLoop() {
    unsigned long now = millis();
    long elapsed = (long)(now - _bot_seg_start);
    const BotSegment &seg = _bot_queue[0].segs[_bot_seg];
    long dur = seg.dur * 4;

    if (elapsed >= dur) {
        _bot_seg_start += dur;  // keeps timing from drifting when loop() runs late
        if (++_bot_seg >= _bot_queue[0].count) {
            for (int i = 1; i < _bot_queued; i++) _bot_queue[i - 1] = _bot_queue[i];
            _bot_seg = 0;
            if (--_bot_queued == 0) {
                botStop();
                return;
            }
            _bot_seg_start = now + BOT_GAP_MILLIS;
        }
        _bot_last_tick = now - BOT_TICK_MILLIS;
        return;
    }

    if (elapsed < 0 || (seg.f0 == 0 && seg.f1 == 0)) {  // gap between sounds, or a rest
        if (_bot_tone_on) {
            noTone(PIN_BUZZER);
            _bot_tone_on = false;
        }
        return;
    }

    if (now - _bot_last_tick < BOT_TICK_MILLIS) return;
    _bot_last_tick = now;

    // exponential glide from f0 to f1, plus vibrato
    float f0 = 200 + seg.f0 * 16;
    float f1 = 200 + seg.f1 * 16;
    float freq = f0 * powf(f1 / f0, (float)elapsed / dur);
    int vib_rate = (seg.mod >> 4) * 4;
    int vib_depth = (seg.mod & 0x0F) * 50;
    if (vib_depth > 0) freq += vib_depth * sinf(2 * PI * vib_rate * elapsed / 1000.0f);
    if (freq < 100) freq = 100;

    tone(PIN_BUZZER, (unsigned int)freq);
    _bot_tone_on = true;
}

void genericBuzzer::startup() {
    play(startup_song);
}

void genericBuzzer::shutdown() {
    play(shutdown_song);
}

void genericBuzzer::quiet(bool buzzer_state) {
    _is_quiet = buzzer_state;
    if (_is_quiet) botStop();
#ifdef PIN_BUZZER_EN
    if (_is_quiet) {
      digitalWrite(PIN_BUZZER_EN, LOW);
    } else {
      digitalWrite(PIN_BUZZER_EN, HIGH);
    }
#endif
}

bool genericBuzzer::isQuiet() {
    return _is_quiet;
}

#endif  // ifdef PIN_BUZZER