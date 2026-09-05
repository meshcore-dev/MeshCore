#pragma once
#include "ConfigSerializer.h"
#include "KeyValueStore.h"

#ifndef MAX_DYNAMIC_CONFG
  #define MAX_DYNAMIC_CONFG  128
#endif

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
