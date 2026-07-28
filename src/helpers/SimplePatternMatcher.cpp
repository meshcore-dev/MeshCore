#include "SimplePatternMatcher.h"

bool SimplePatternMatcher::matches(const char* pattern, const char* text) {
  if (pattern == nullptr || text == nullptr) return false;
  return matchHere(pattern, text);
}

bool SimplePatternMatcher::matchHere(const char* pattern, const char* text) {
  const char* starPattern = nullptr;
  const char* starText = nullptr;

  while (*text) {
    if (*pattern == '*') {
      while (*pattern == '*') pattern++;
      if (*pattern == '\0') return true;

      starPattern = pattern;
      starText = text;
    } else if (*pattern == '?' || *pattern == *text) {
      pattern++;
      text++;
    } else if (starPattern != nullptr) {
      pattern = starPattern;
      text = ++starText;
    } else {
      return false;
    }
  }

  while (*pattern == '*') pattern++;

  return *pattern == '\0';
}