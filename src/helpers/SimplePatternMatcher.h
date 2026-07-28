#pragma once

class SimplePatternMatcher {
public:
  static bool matches(const char* pattern, const char* text);

private:
  static bool matchHere(const char* pattern, const char* text);
};