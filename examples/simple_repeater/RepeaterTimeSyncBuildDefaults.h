#pragma once

#include <stddef.h>
#include <stdint.h>

// Ordinary upstream builds retain the existing opt-in behaviour. Private
// deployments can replace these literals through PlatformIO build flags.
#ifndef REPEATER_DEFAULT_TIME_SYNC_CHANNEL
  #define REPEATER_DEFAULT_TIME_SYNC_CHANNEL ""
#endif

#ifndef REPEATER_DEFAULT_TIME_SYNC_DISPLAY_NAME
  #define REPEATER_DEFAULT_TIME_SYNC_DISPLAY_NAME ""
#endif

#ifndef REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY
  #define REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY ""
#endif

#ifndef REPEATER_DEFAULT_TIME_SYNC_ENABLED
  #define REPEATER_DEFAULT_TIME_SYNC_ENABLED 0
#endif

namespace repeater_time_sync_build_defaults {

// C++11-compatible constexpr validators make invalid deployment literals fail
// during compilation rather than leaving an unattended repeater misconfigured.
constexpr bool isHexCharacter(char value) {
  return (value >= '0' && value <= '9') ||
         (value >= 'a' && value <= 'f') ||
         (value >= 'A' && value <= 'F');
}

constexpr bool isHexPublicKey(const char* value, size_t index = 0) {
  return index == 64 ? value[index] == 0
                     : isHexCharacter(value[index]) && isHexPublicKey(value, index + 1);
}

constexpr bool isSafeDisplayName(const char* value, size_t index = 0) {
  return value[index] == 0 ? index > 0
                           : index < 20 && value[index] != '\r' && value[index] != '\n' &&
                             isSafeDisplayName(value, index + 1);
}

constexpr bool isPublicChannel(const char* value, size_t index = 0) {
  return "public"[index] == 0 ? value[index] == 0
                              : value[index] == "public"[index] && isPublicChannel(value, index + 1);
}

constexpr bool isSafeHashtagChannel(const char* value, size_t index = 1) {
  return value[index] == 0 ? index > 1
                           : index < 31 && value[index] != '\r' && value[index] != '\n' &&
                             isSafeHashtagChannel(value, index + 1);
}

constexpr bool isSupportedChannel(const char* value) {
  return isPublicChannel(value) || (value[0] == '#' && isSafeHashtagChannel(value));
}

constexpr const char* CHANNEL = REPEATER_DEFAULT_TIME_SYNC_CHANNEL;
constexpr const char* DISPLAY_NAME = REPEATER_DEFAULT_TIME_SYNC_DISPLAY_NAME;
constexpr const char* PUBLIC_KEY = REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY;
constexpr uint8_t ENABLED = REPEATER_DEFAULT_TIME_SYNC_ENABLED;

} // namespace repeater_time_sync_build_defaults

static_assert(REPEATER_DEFAULT_TIME_SYNC_ENABLED == 0 || REPEATER_DEFAULT_TIME_SYNC_ENABLED == 1,
              "REPEATER_DEFAULT_TIME_SYNC_ENABLED must be 0 or 1");
static_assert(sizeof(REPEATER_DEFAULT_TIME_SYNC_CHANNEL) <= 32,
              "REPEATER_DEFAULT_TIME_SYNC_CHANNEL must fit in 31 characters");
static_assert(sizeof(REPEATER_DEFAULT_TIME_SYNC_DISPLAY_NAME) <= 21,
              "REPEATER_DEFAULT_TIME_SYNC_DISPLAY_NAME must fit in 20 characters");
static_assert(sizeof(REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY) == 1 ||
                sizeof(REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY) == 65,
              "REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY must be empty or 64 hexadecimal characters");
static_assert(sizeof(REPEATER_DEFAULT_TIME_SYNC_CHANNEL) == 1 ||
                repeater_time_sync_build_defaults::isSupportedChannel(REPEATER_DEFAULT_TIME_SYNC_CHANNEL),
              "time-sync channel must be empty, public, or a non-empty hashtag channel");
static_assert(sizeof(REPEATER_DEFAULT_TIME_SYNC_DISPLAY_NAME) == 1 ||
                repeater_time_sync_build_defaults::isSafeDisplayName(REPEATER_DEFAULT_TIME_SYNC_DISPLAY_NAME),
              "time-sync display name must be empty or a valid display name");
static_assert(sizeof(REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY) == 1 ||
                repeater_time_sync_build_defaults::isHexPublicKey(REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY),
              "time-sync public key must be empty or hexadecimal");
static_assert(!REPEATER_DEFAULT_TIME_SYNC_ENABLED ||
                (sizeof(REPEATER_DEFAULT_TIME_SYNC_CHANNEL) > 1 &&
                 sizeof(REPEATER_DEFAULT_TIME_SYNC_DISPLAY_NAME) > 1 &&
                 sizeof(REPEATER_DEFAULT_TIME_SYNC_PUBLIC_KEY) == 65),
              "enabled time sync requires a channel, display name, and public key");
