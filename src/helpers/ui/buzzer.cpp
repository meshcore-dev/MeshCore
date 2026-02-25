#ifdef PIN_BUZZER
#include "buzzer.h"

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
  startup();
}

void genericBuzzer::play(const char *melody) {
  if (isPlaying()) // interrupt existing
  {
    rtttl::stop();
  }

  if (_is_quiet) return;

  rtttl::begin(PIN_BUZZER, melody);
  //    Serial.print("DBG: Playing melody - isQuiet: ");
  //    Serial.println(isQuiet());
}

bool genericBuzzer::isPlaying() {
  return rtttl::isPlaying();
}

void genericBuzzer::loop() {
  if (!rtttl::done()) rtttl::play();
}

void genericBuzzer::startup() {
  play(startup_song);
}

void genericBuzzer::shutdown() {
  play(shutdown_song);
}

void genericBuzzer::quiet(bool buzzer_state) {
  _is_quiet = buzzer_state;
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

#endif // ifdef PIN_BUZZER