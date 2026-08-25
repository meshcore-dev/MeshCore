#pragma once

#include <Arduino.h>

#ifndef CONFIG_MAX_DEPTH
  #define CONFIG_MAX_DEPTH   8
#endif

#ifndef CONFIG_MAX_KEYLEN
  #define CONFIG_MAX_KEYLEN  16
#endif

#ifndef CONFIG_MAX_TOKEN_LEN
  #define CONFIG_MAX_TOKEN_LEN   128
#endif

class ConfigSerializer {
  bool _first;
  int8_t _depth;

  enum OP { READ, WRITE };

  class Context {
    Stream* _f;
    OP _op;
    uint8_t rd_len;
    uint8_t rd_mode;
    char pending;
    char rd_buf[CONFIG_MAX_TOKEN_LEN];
    char _keys[CONFIG_MAX_DEPTH][CONFIG_MAX_KEYLEN];

  public:
    bool success = true;
    Context(Stream* f, OP op) : _f(f), _op(op) { rd_buf[rd_len = 0] = 0; rd_mode = 0; pending = 0; }
    OP op() const { return _op; }
    Stream* file() const { return _f; }
    int readNext();
    const char* getToken() const { return rd_buf; }
    bool keyMatch(int8_t depth, const char* key) { return strcmp(key, _keys[depth]) == 0; }
    void setKey(uint8_t depth, const char* key) { strcpy(_keys[depth], key);  }
  };

  Context* _context = NULL;

  void writeComma();

protected:
  ConfigSerializer() { }

  void def(const char* key, char* value, size_t max_len);  // max_len inclusive of null
  void def(const char* key, void* value, size_t len);  // binary blob (encoded in hex)
  void def(const char* key, int32_t& value);
  void def(const char* key, int16_t& value);
  void def(const char* key, int8_t& value);
  void def(const char* key, uint32_t& value);
  void def(const char* key, uint16_t& value);
  void def(const char* key, uint8_t& value);
  void def(const char* key, float& value);
  void def(const char* key, double& value);
  void def(const char* key, bool& value);
  void def(const char* key, ConfigSerializer& sub_obj);

  virtual void structure() = 0;

public:
  bool loadSerial(Stream& s);
  bool saveSerial(Stream& s);
};
