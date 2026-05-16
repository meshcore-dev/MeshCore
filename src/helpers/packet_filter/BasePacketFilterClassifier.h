#pragma once

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

class BasePacketFilterClassifier {
public:
  typedef bool (*BlockRuleFn)(void* context, const char* classifier_name, char* args);

  static bool isArgSpace(char c) {
    return c == ' ' || c == '\t';
  }

  static char* nextToken(char*& cursor) {
    if (cursor == NULL) return NULL;
    while (isArgSpace(*cursor)) cursor++;
    if (*cursor == 0) return NULL;

    char* token = cursor;
    while (*cursor && !isArgSpace(*cursor)) cursor++;
    if (*cursor) *cursor++ = 0;
    return token;
  }

  static char* remainingArgs(char*& cursor) {
    if (cursor == NULL) return NULL;
    while (isArgSpace(*cursor)) cursor++;
    return cursor;
  }

  static char* unquote(char* value) {
    if (value == NULL || *value != '"') return value;

    value++;
    char* end = strrchr(value, '"');
    if (end != NULL) *end = 0;
    return value;
  }

  static bool parseUnsigned(const char* token, unsigned long min_value, unsigned long max_value, unsigned long* value) {
    if (token == NULL || value == NULL) return false;

    char* end = NULL;
    unsigned long parsed = strtoul(token, &end, 10);
    if (end == token || *end != 0 || parsed < min_value || parsed > max_value) return false;

    *value = parsed;
    return true;
  }

  static bool handleBlockCommand(const char* classifier_name, char* args, char* reply, size_t reply_len,
                                 const char* usage, const char* success_message,
                                 BlockRuleFn block_rule, void* block_context) {
    char* classifier_args = remainingArgs(args);
    if (classifier_args == NULL || *classifier_args == 0 || strcmp(classifier_args, "help") == 0) {
      snprintf(reply, reply_len, "%s", usage);
      return false;
    }

    if (block_rule != NULL && block_rule(block_context, classifier_name, classifier_args)) {
      snprintf(reply, reply_len, "%s", success_message);
      return true;
    }

    snprintf(reply, reply_len, "Err - packet filter rule invalid or full");
    return false;
  }
};
