#pragma once

#include <Arduino.h>
#include <NonBlockingRtttl.h>

/* class abstracts underlying RTTTL library 

    Just a simple implementation to start.  At the moment use same
    melody for message and discovery
    Suggest enum type for different sounds
    - on message
    - on discovery

    TODO
    - make message ring tone configurable

*/

struct BotSegment;

#define BOT_QUEUE_SIZE  4

class genericBuzzer
{
    public:
        void begin();  // set up buzzer port
        void play(const char *melody); // Generic play function
        void playBot(const BotSegment *segs, uint8_t count);  // play a bot speak sound (see botspeak_sounds.h)
        int playWords(const char *text);  // queue bot speak sounds for words found in text, returns number queued
        void loop();  // loop driven-nonblocking
        void startup();  // play startup sound
        void shutdown();  // play shutdown sound
        bool isPlaying();  // returns true if a sound is still playing else false
        void quiet(bool buzzer_state);  // enables or disables the buzzer
        bool isQuiet();  // get buzzer state on/off

    private:
        // gemini's picks:
        const char *startup_song = "Startup:d=4,o=5,b=160:16c6,16e6,8g6";
        const char *shutdown_song = "Shutdown:d=4,o=5,b=100:8g5,16e5,16c5";

        bool _is_quiet = true;

        // bot speak player: sweeps tone() frequency every few millis
        struct BotSound { const BotSegment *segs; uint8_t count; };
        BotSound _bot_queue[BOT_QUEUE_SIZE];
        uint8_t _bot_queued = 0;     // sounds in queue, [0] is playing
        uint8_t _bot_seg = 0;        // current segment in _bot_queue[0]
        unsigned long _bot_seg_start = 0;
        unsigned long _bot_last_tick = 0;
        bool _bot_tone_on = false;

        void botStart(const BotSound *sounds, int count);
        void botStop();
        void botLoop();
};
