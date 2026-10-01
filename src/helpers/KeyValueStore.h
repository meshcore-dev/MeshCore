#pragma once
#include <stdint.h>
#include <string.h>

class KeyValueStore {
protected:
  KeyValueStore() { }
public:
  virtual bool setByKey(const char* key, const char* value) { return false; }
  virtual bool getByKey(const char* key, char* value, size_t max_len) { return false; }
};
