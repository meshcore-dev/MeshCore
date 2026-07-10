/*
 * Zephyr replacement for rweather/Crypto's RNG.cpp. See RNG.h for why.
 *
 * SPDX-License-Identifier: MIT
 */

#include "RNG.h"

#include <zephyr/random/random.h>

void RNGClass::rand(uint8_t *data, size_t len) {
  sys_rand_get(data, len);
}

RNGClass RNG;
