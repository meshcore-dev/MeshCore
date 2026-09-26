#include "DynamicConfigSerializer.h"
#include <Utils.h>

#define PROP_SEP_CHAR   '|'
#define PROP_SEP_STR    "|"
#define KEY_SEP_CHAR    ':'
#define KEY_SEP_STR     ":"

bool DynamicConfigSerializer::setByKeyPrv(const char* key, const char* value) {
  if (_fallback && _fallback->setByKey(key, value)) return true;

  // TODO: guard for bad chars (':' or '|')
  char tmp[MAX_DYNAMIC_CONFG];
  strcpy(tmp, _config);  // make a (modifiable) copy

  const char* parts[8];
  int n = mesh::Utils::parseTextParts(tmp, parts, 8, PROP_SEP_CHAR);

  // add/replace in _config[]
  char new_config[MAX_DYNAMIC_CONFG];
  new_config[0] = 0;

  int keylen = strlen(key);
  for (int i = 0; i < n; i++) {
    const char* item = parts[i];
    if (item[keylen] == KEY_SEP_CHAR && memcmp(item, key, keylen) == 0) {
      // key exists, so omit old value from this pass (will append new value at end)
    } else {
      if (new_config[0]) {
        strcat(new_config, PROP_SEP_STR);
      }
      strcat(new_config, item);
    }
  }
  // now append new key/value (if it fits)
  if (strlen(new_config) + strlen(key) + strlen(value) + 2 < sizeof(_config)-1) {
    if (new_config[0]) {
      strcat(new_config, PROP_SEP_STR);
    }
    strcat(new_config, key);
    strcat(new_config, KEY_SEP_STR);
    strcat(new_config, value);
    strcpy(_config, new_config);  // commit new serialized string
    return true;
  }
  return false;   // didn't fit in _config[]
}

bool DynamicConfigSerializer::setByKey(const char* key, const char* value) {
  if (setByKeyPrv(key, value)) {
    markDirty();
    return true;
  }
  return false;
}

bool DynamicConfigSerializer::getByKey(const char* key, char* value, size_t max_len) {
  if (_fallback && _fallback->getByKey(key, value, max_len)) return true;

  char tmp[MAX_DYNAMIC_CONFG];
  strcpy(tmp, _config);  // make a (modifiable) copy

  const char* parts[8];
  int n = mesh::Utils::parseTextParts(tmp, parts, 8, PROP_SEP_CHAR);

  int keylen = strlen(key);
  for (int i = 0; i < n; i++) {
    const char* item = parts[i];
    if (item[keylen] == KEY_SEP_CHAR && memcmp(item, key, keylen) == 0) {
      strncpy(value, &item[keylen+1], max_len);
      value[max_len] = 0;
      return true;
    }
  }
  return false;
}

void DynamicConfigSerializer::structure() {
  if (_context->op() == OP::WRITE) {
    char tmp[MAX_DYNAMIC_CONFG];
    strcpy(tmp, _config);  // make a (modifiable) copy

    const char* parts[8];
    int n = mesh::Utils::parseTextParts(tmp, parts, 8, PROP_SEP_CHAR);

    for (int i = 0; i < n; i++) {
      char* item = (char *) parts[i];
      char* eq = strchr(item, KEY_SEP_CHAR);
      if (eq) {
        *eq = 0;   // replace separator with null terminator
        def(item, eq + 1, MAX_DYNAMIC_CONFG/2);
      }
    }
  } else {
    setByKeyPrv(_context->getKey(getDepth()), _context->getToken());
  }
}
