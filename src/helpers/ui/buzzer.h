#pragma once

#include <Arduino.h>
#include <NonBlockingRtttl.h>

/* class abstracts underlying RTTTL library 

    Just a simple imlementation to start.  At the moment use same
    melody for message and discovery
    Suggest enum type for different sounds
    - on message
    - on discovery

    TODO
    - make message ring tone configurable

*/

class genericBuzzer
{
    public:
        void begin();  // set up buzzer port
        void play(const char *melody); // Generic play function
        void loop();  // loop driven-nonblocking
        void startup();  // play startup sound
        void shutdown();  // play shutdown sound
        bool isPlaying();  // returns true if a sound is still playing else false
        void quiet(bool buzzer_state);  // enables or disables the buzzer
        bool isQuiet();  // get buzzer state on/off


        // Example RTTTL melodies for startup/shutdown/message/discovery
        const char *startup_song    = "Startup:d=4,o=5,b=160:16c6,16e6,8g6";
        const char *shutdown_song   = "Shutdown:d=4,o=5,b=100:8g5,16e5,16c5";
        const char *message_song    = "MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7";
        const char *discovery_song  = "Discovery:d=4,o=5,b=180:8e6,8d6,8c6";
        const char *channel_song    = "kerplop:d=16,o=6,b=120:32g#,16c#"; // more of a "plop" sound for channel change
        const char *ack_song        = "ack:d=16,o=8,b=120:c8,c6"; // Two beeps: first high (C8), then low (C6)
    private:
        // gemini's picks:
        const char *startup_song = "Startup:d=4,o=5,b=160:16c6,16e6,8g6";
        const char *shutdown_song = "Shutdown:d=4,o=5,b=100:8g5,16e5,16c5";

        bool _is_quiet = true;
};
