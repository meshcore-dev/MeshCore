#pragma once

#if defined(ESP32) || defined(RP2040_PLATFORM)
  #include <FS.h>
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <Adafruit_LittleFS.h>
  using namespace Adafruit_LittleFS_Namespace;
#endif

namespace checked_io {

inline bool readExact(File& file, uint8_t* dest, size_t len) {
  size_t done = 0;
  while (done < len) {
    int n = file.read(dest + done, len - done);
    if (n <= 0) return false;
    done += (size_t) n;
  }
  return true;
}

inline bool writeExact(File& file, const uint8_t* src, size_t len) {
  size_t done = 0;
  while (done < len) {
    size_t n = file.write(src + done, len - done);
    if (n == 0) return false;
    done += n;
  }
  return true;
}

template <typename T>
inline bool readValue(File& file, T& value) {
  return readExact(file, (uint8_t*) &value, sizeof(T));
}

template <typename T>
inline bool writeValue(File& file, const T& value) {
  return writeExact(file, (const uint8_t*) &value, sizeof(T));
}

}
