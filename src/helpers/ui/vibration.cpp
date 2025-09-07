#ifdef PIN_VIBRATION
#include "vibration.h"

void genericVibration::begin()
{
    pinMode(PIN_VIBRATION, OUTPUT);
    digitalWrite(PIN_VIBRATION, LOW);
    duration = 0;
}

void genericVibration::trigger()
{
    duration = millis();
    digitalWrite(PIN_VIBRATION, HIGH);
}

void genericVibration::loop()
{
    if (isVibrating()) {
        if ((millis() / 1000) % 2 == 0) {
            digitalWrite(PIN_VIBRATION, LOW);
        } else {
            digitalWrite(PIN_VIBRATION, HIGH);
        }

        if (millis() - duration > VIBRATION_TIMEOUT) {
            stop();
        }
    }
}

bool genericVibration::isVibrating()
{
    return duration > 0;
}

void genericVibration::stop()
{
    duration = 0;
    digitalWrite(PIN_VIBRATION, LOW);
}

#endif // ifdef PIN_VIBRATION
