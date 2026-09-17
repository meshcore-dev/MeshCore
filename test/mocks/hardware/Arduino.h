#pragma once

#include "../Arduino.h"
#include <algorithm>

#define LOW 0
#define HIGH 1
#define INPUT 1
#define OUTPUT 3
#define INPUT_PULLUP 5
#define MSBFIRST 1
#define SPI_MODE0 0

using std::min;
using std::max;
inline long random(long low, long high) { return low; }
