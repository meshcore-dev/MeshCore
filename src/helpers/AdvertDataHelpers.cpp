#include <helpers/AdvertDataHelpers.h>
#include <helpers/UTF8Helpers.h>

  uint8_t AdvertDataBuilder::encodeTo(uint8_t app_data[]) {
    app_data[0] = _type;
    int i = 1;
    if (_has_loc) {
      app_data[0] |= ADV_LATLON_MASK;
      memcpy(&app_data[i], &_lat, 4); i += 4;
      memcpy(&app_data[i], &_lon, 4); i += 4;
    }
    if (_extra1) {
      app_data[0] |= ADV_FEAT1_MASK;
      memcpy(&app_data[i], &_extra1, 2); i += 2;
    }
    if (_extra2) {
      app_data[0] |= ADV_FEAT2_MASK;
      memcpy(&app_data[i], &_extra2, 2); i += 2;
    }
    if (_name && *_name != 0) {
      size_t name_len = mesh::validUtf8PrefixLength(_name, MAX_ADVERT_DATA_SIZE - i);
      if (name_len > 0) {
        app_data[0] |= ADV_NAME_MASK;
        memcpy(&app_data[i], _name, name_len);
        i += name_len;
      }
    }
    return i;
  }

static uint32_t readUtf8(const char* text, size_t* advance) {
  const uint8_t first = (uint8_t)text[0];
  size_t sequence_length = 1;
  uint32_t codepoint = first;
  if (first >= 0xC2 && first <= 0xDF) {
    sequence_length = 2;
    codepoint = first & 0x1F;
  } else if (first >= 0xE0 && first <= 0xEF) {
    sequence_length = 3;
    codepoint = first & 0x0F;
  } else if (first >= 0xF0 && first <= 0xF4) {
    sequence_length = 4;
    codepoint = first & 0x07;
  }
  for (size_t i = 1; i < sequence_length; i++) {
    codepoint = (codepoint << 6) | ((uint8_t)text[i] & 0x3F);
  }
  *advance = sequence_length;
  return codepoint;
}

bool AdvertDataParser::isValidName(const char *n) {
  // node_name and contact names are 32 bytes including the NUL. Rejecting a
  // longer string here avoids storing a codepoint cut in half by strncpy.
  if (n == nullptr) return false;
  size_t len = strlen(n);
  if (len == 0) return true;
  if (len > 31) return false;
  if (mesh::validUtf8PrefixLength(n, len) != len) return false;

  const char* p = n;
  while (*p) {
    uint8_t c = (uint8_t)*p;
    if (c < 0x20 || c == 0x7F) return false;
    if (c < 0x80) {
      // Prefs are stored as key:value|key:value. These bytes also break the
      // web configurator when they land in that text or in markup.
      if (c == '[' || c == ']' || c == '\\' || c == ':' || c == ',' || c == '?' ||
          c == '*' || c == '|' || c == '<' || c == '>' || c == '"' || c == '\'' ||
          c == '&' || c == '/') return false;
      p++;
      continue;
    }
    size_t advance = 1;
    uint32_t cp = readUtf8(p, &advance);
    // Letterlike symbols include U+2122 (™), which the configurator cannot load.
    if (cp >= 0x2000 && cp <= 0x2BFF) return false;
    p += advance;
  }
  return true;
}

  AdvertDataParser::AdvertDataParser(const uint8_t app_data[], uint8_t app_data_len) {
    _name[0] = 0;
    _lat = _lon = 0;
    _flags = app_data[0];
    _valid = false;
    _extra1 = _extra2 = 0;
  
    int i = 1;
    if (_flags & ADV_LATLON_MASK) {
      memcpy(&_lat, &app_data[i], 4); i += 4;
      memcpy(&_lon, &app_data[i], 4); i += 4;
    }
    if (_flags & ADV_FEAT1_MASK) {
      memcpy(&_extra1, &app_data[i], 2); i += 2;
    }
    if (_flags & ADV_FEAT2_MASK) {
      memcpy(&_extra2, &app_data[i], 2); i += 2;
    }

    if (app_data_len >= i) {
      int nlen = 0;
      if (_flags & ADV_NAME_MASK) {
        nlen = app_data_len - i;  // remainder of app_data
      }
      if (nlen > 0) {
        memcpy(_name, &app_data[i], nlen);
        _name[nlen] = 0;  // set null terminator
      }
      _valid = true;
    }
  }

#include <Arduino.h>

void AdvertTimeHelper::formatRelativeTimeDiff(char dest[], int32_t seconds_from_now, bool short_fmt) {
  const char *suffix;
  if (seconds_from_now < 0) {
    suffix = short_fmt ? "" : " ago";
    seconds_from_now = -seconds_from_now;
  } else {
    suffix = short_fmt ? "" : " from now";
  }

  if (seconds_from_now < 60) {
    sprintf(dest, "%d secs %s", seconds_from_now, suffix);
  } else {
    int32_t mins = seconds_from_now / 60;
    if (mins < 60) {
      sprintf(dest, "%d mins %s", mins, suffix);
    } else {
      int32_t hours = mins / 60;
      if (hours < 24) {
        sprintf(dest, "%d hours %s", hours, suffix);
      } else {
        sprintf(dest, "%d days %s", hours / 24, suffix);
      }
    }
  }
}
