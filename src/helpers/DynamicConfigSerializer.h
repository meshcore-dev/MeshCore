#pragma once
#include "ConfigSerializer.h"
#include "KeyValueStore.h"

#ifndef MAX_DYNAMIC_CONFG
  #define MAX_DYNAMIC_CONFG  128
#endif

#ifdef CONFIG_SERIALIZER_NOOP
// No local text store: keys go to the fallback store only.
class DynamicConfigSerializer : public ConfigSerializer, public KeyValueStore {
  KeyValueStore* _fallback;

protected:
  void structure() override { }

public:
  DynamicConfigSerializer(KeyValueStore* fallback = NULL) : _fallback(fallback) { }

  bool setByKey(const char* key, const char* value) override {
    if (_fallback && _fallback->setByKey(key, value)) { markDirty(); return true; }
    return false;
  }
  bool getByKey(const char* key, char* value, size_t max_len) override {
    return _fallback && _fallback->getByKey(key, value, max_len);
  }
};
#else
class DynamicConfigSerializer : public ConfigSerializer, public KeyValueStore {
  char _config[MAX_DYNAMIC_CONFG];
  KeyValueStore* _fallback;

  bool setByKeyPrv(const char* key, const char* value);

protected:
  void structure() override;

public:
  DynamicConfigSerializer(KeyValueStore* fallback = NULL) : _fallback(fallback) { _config[0] = 0; }

  bool setByKey(const char* key, const char* value) override;
  bool getByKey(const char* key, char* value, size_t max_len) override;
};
#endif  // CONFIG_SERIALIZER_NOOP
