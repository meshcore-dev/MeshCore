#pragma once

// Board audio backend for the Wio Tracker L2 Pro (ES8311 codec + speaker).
// UI code includes "Sound.h" and gets whatever the variant provides; all
// functions are safe no-ops if the codec failed to initialize.

#define SOUND_TONE_STYLES 6   // Classic, Chirp, Ding dong, Trill, Rise, Alarm

void soundInit();          // call once after Wire is up (board.begin done)
void soundSetAmpControl(void (*fn)(bool on));   // board hook to gate the speaker amp power
bool soundReady();
void soundBeep(int freq_hz, int duration_ms);   // short blocking tone
void soundMessageTone();   // incoming DM (plays the selected alert style)
void soundChannelTone();   // incoming channel message (softer variant)
void soundAckTone();       // delivery ack / confirm

void soundLoadTonePref();          // read /tone from SPIFFS (safe before soundInit)
void soundSetToneStyle(int style); // select + persist an alert style
int  soundGetToneStyle();
