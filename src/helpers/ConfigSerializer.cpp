#include "ConfigSerializer.h"

bool ConfigSerializer::saveSerial(Stream& s) {
  Context context(&s, OP::WRITE);
  _context = &context;  // set the context for structure() call
  s.print("{");   // root object
  _first = true;
  structure();
  if (s.print("}") != 1) context.success = false;  // failure detect
  _context = NULL;
  return context.success;
}

#define TOK_ERROR     -1
#define TOK_EOF        0
#define TOK_KEY        1
#define TOK_VALUE      2
#define TOK_START_OBJ  3
#define TOK_END_OBJ    4
#define TOK_WHITESPACE 5

static bool is_whitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
static bool is_key_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
static bool is_value_char(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '-' || c == '.';
}

#define EXPECT_OPEN_BRACE   0
#define EXPECT_KEY          1
#define EXPECT_VAL_OR_OBJ   2
#define EXPECT_STRING_VAL   3
#define EXPECT_STRING_ESCAPE   4
#define EXPECT_COMMA_OR_CLOSE  5
#define EXPECT_COMMA_OR_KEY    6
#define EXPECT_COMMA_OR_KEY_OR_CLOSE  7

int ConfigSerializer::Context::readNext() {
  char c;
  if (pending) {
    c = pending;
    pending = 0;
  } else {
    if (_f->available() == 0) return TOK_EOF;

    int n = _f->read();
    if (n < 0) return TOK_EOF;
    c = (char)n;
  }

  switch (rd_mode) {
    case EXPECT_OPEN_BRACE:
      if (c == '{') { rd_mode = EXPECT_KEY; return TOK_START_OBJ; }
      if (is_whitespace(c)) return TOK_WHITESPACE;
      return TOK_ERROR;

    case EXPECT_COMMA_OR_KEY_OR_CLOSE:
      if (c == '}') { rd_mode = EXPECT_COMMA_OR_KEY_OR_CLOSE; return TOK_END_OBJ; }
    case EXPECT_COMMA_OR_KEY:
      if (c == ',') { rd_mode = EXPECT_KEY; return TOK_WHITESPACE; }
    case EXPECT_KEY:
      if (rd_len > 0 && c == ':') { rd_buf[rd_len] = 0; rd_len = 0; rd_mode = EXPECT_VAL_OR_OBJ; return TOK_KEY; }
      if (rd_len == 0 && is_whitespace(c)) return TOK_WHITESPACE;
      if (rd_len < CONFIG_MAX_KEYLEN-1 && is_key_char(c)) { rd_buf[rd_len++] = c; return TOK_WHITESPACE; }
      return TOK_ERROR;

    case EXPECT_VAL_OR_OBJ:
      if (rd_len == 0 && is_whitespace(c)) return TOK_WHITESPACE;
      if (rd_len == 0 && c == '"') { rd_mode = EXPECT_STRING_VAL; return TOK_WHITESPACE; }
      if (rd_len == 0 && c == '{') { rd_mode = EXPECT_KEY; return TOK_START_OBJ; }
      if (is_value_char(c) && rd_len < CONFIG_MAX_TOKEN_LEN-1) { rd_buf[rd_len++] = c; return TOK_WHITESPACE; }
      if (rd_len > 0 && (c == ',' || c == '}' || is_whitespace(c))) { pending = c; rd_buf[rd_len] = 0; rd_len = 0; rd_mode = EXPECT_COMMA_OR_CLOSE; return TOK_VALUE;  }
      return TOK_ERROR;

    case EXPECT_STRING_ESCAPE:
      if ((c == 'n') && rd_len < CONFIG_MAX_TOKEN_LEN-1) { rd_buf[rd_len++] = '\n'; rd_mode = EXPECT_STRING_VAL; return TOK_WHITESPACE; }
      if ((c == 'r') && rd_len < CONFIG_MAX_TOKEN_LEN-1) { rd_buf[rd_len++] = '\r'; rd_mode = EXPECT_STRING_VAL; return TOK_WHITESPACE; }
      if ((c == '"' || c == '\\' || c == '/') && rd_len < CONFIG_MAX_TOKEN_LEN-1) { rd_buf[rd_len++] = c; rd_mode = EXPECT_STRING_VAL; return TOK_WHITESPACE; }
      return TOK_ERROR;  // unsupport escape

    case EXPECT_STRING_VAL:
      if (c == '"') { rd_buf[rd_len] = 0; rd_len = 0; rd_mode = EXPECT_COMMA_OR_CLOSE; return TOK_VALUE; }
      if (c == '\\') { rd_mode = EXPECT_STRING_ESCAPE; return TOK_WHITESPACE; }
      if (rd_len < CONFIG_MAX_TOKEN_LEN-1) { rd_buf[rd_len++] = c; return TOK_WHITESPACE; }
      return TOK_ERROR;

    case EXPECT_COMMA_OR_CLOSE:
      if (c == ',') { rd_mode = EXPECT_KEY; return TOK_WHITESPACE; }
      if (c == '}') { rd_mode = EXPECT_COMMA_OR_KEY_OR_CLOSE; return TOK_END_OBJ; }
      if (is_whitespace(c)) return TOK_WHITESPACE;
      return TOK_ERROR;
  }
  return TOK_ERROR;   // unknown mode
}

bool ConfigSerializer::loadSerial(Stream& s) {
  Context context(&s, OP::READ);
  _context = &context;  // set the context for structure() call
  uint8_t sp = 0;   // object nesting stack pointer
  int next_tok;

  // parse the Json file
  while ((next_tok = context.readNext()) > TOK_EOF) {
    if (next_tok == TOK_KEY) {
      context.setKey(sp, context.getToken());
    } else if (next_tok == TOK_VALUE) {
      _depth = 1;  // re-run the structure() hierarchy again (looking for specific key, at specific depth)
      structure();
    } else if (next_tok == TOK_START_OBJ) {
      if (sp < CONFIG_MAX_DEPTH - 1) {
        sp++;
      } else {
        //Serial.printf("Error: max nesting reached"); // TODO: debug logging
        context.success = false;
        break;
      }
    } else if (next_tok == TOK_END_OBJ) {
      if (sp > 0) {
        sp--;
      } else {
        //Serial.printf("Error: too many closing '}'"); // TODO: debug logging
        context.success = false;
        break;
      }
    }
  }
  if (sp != 0 || next_tok == TOK_ERROR) {
    context.success = false;   // unmatched { }, or other parse error
  }
  _context = NULL;
  return context.success;
}

void ConfigSerializer::writeComma() {
  if (_first) {
    _first = false;
  } else {
    _context->file()->print(",");  // comma separated properties
  }
}

#include <Utils.h>

void ConfigSerializer::def(const char* key, void* value, size_t len) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":\"");
    mesh::Utils::printHex(*_context->file(), (uint8_t*) value, len);
    _context->file()->print("\"");
  } else {
    if (_context->keyMatch(_depth, key)) {
      memset(value, 0, len);
      mesh::Utils::fromHex((uint8_t *)value, len, _context->getToken());
    }
  }
}

void ConfigSerializer::def(const char* key, char* value, size_t max_len) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":\"");
    char c;
    while ((c = *value++) != 0) {  // TODO: handle UTF-8 encoding
      if (c == '"') {
        _context->file()->print("\\\"");
      } else if (c == '\\') {
        _context->file()->print("\\\\");
      } else if (c == '\n') {
        _context->file()->print("\\n");
      } else if (c == '\r') {
        _context->file()->print("\\r");
      } else {
        _context->file()->print(c);
      }
    }
    _context->file()->print("\"");
  } else {
    if (_context->keyMatch(_depth, key)) {
      strncpy(value, _context->getToken(), max_len - 1);
      value[max_len - 1] = 0;
    }
  }
}

void ConfigSerializer::def(const char* key, int32_t& value) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":");
    _context->file()->print(value);
  } else {
    if (_context->keyMatch(_depth, key)) {
      value = atol(_context->getToken());
    }
  }
}

void ConfigSerializer::def(const char* key, uint32_t& value) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":");
    _context->file()->print(value);
  } else {
    if (_context->keyMatch(_depth, key)) {
      value = atol(_context->getToken());
    }
  }
}

void ConfigSerializer::def(const char* key, int16_t& value) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":");
    _context->file()->print((int32_t) value, 10);
  } else {
    if (_context->keyMatch(_depth, key)) {
      value = atol(_context->getToken());
    }
  }
}

void ConfigSerializer::def(const char* key, uint16_t& value) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":");
    _context->file()->print((uint32_t) value, 10);
  } else {
    if (_context->keyMatch(_depth, key)) {
      value = atoi(_context->getToken());
    }
  }
}

void ConfigSerializer::def(const char* key, uint8_t& value) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":");
    _context->file()->print((uint32_t) value, 10);
  } else {
    if (_context->keyMatch(_depth, key)) {
      value = atoi(_context->getToken());
    }
  }
}

void ConfigSerializer::def(const char* key, int8_t& value) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":");
    _context->file()->print((int32_t) value, 10);
  } else {
    if (_context->keyMatch(_depth, key)) {
      value = atoi(_context->getToken());
    }
  }
}

void ConfigSerializer::def(const char* key, bool& value) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":");
    _context->file()->print(value ? "true" : "false");
  } else {
    if (_context->keyMatch(_depth, key)) {
      value = strcmp(_context->getToken(), "true") == 0 || atoi(_context->getToken()) != 0;  // 'true' or a non-zero number
    }
  }  
}

void ConfigSerializer::def(const char* key, double& value) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":");
    if (value == 0.0) {
      _context->file()->print("0");  // shorter encoding
    } else {
      _context->file()->print(value, 6);  // REVISIT: how many dec places?
    }
  } else {
    if (_context->keyMatch(_depth, key)) {
      value = atof(_context->getToken());
    }
  }
}

void ConfigSerializer::def(const char* key, float& value) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":");
    if (value == 0.0f) {
      _context->file()->print("0");  // shorter encoding
    } else {
      _context->file()->print(value, 4);  // REVISIT: how many dec places?
    }
  } else {
    if (_context->keyMatch(_depth, key)) {
      value = (float) atof(_context->getToken());
    }
  }
}

void ConfigSerializer::def(const char* key, ConfigSerializer& sub_obj) {
  if (_context->op() == OP::WRITE) {
    writeComma();
    _context->file()->print(key);
    _context->file()->print(":{");
    sub_obj._context = _context;  // inherit the Context
    sub_obj._first = true;
    sub_obj.structure();   // recurse into sub object
    if (_context->file()->print("}") != 1) _context->success = false;  // failure detect
  } else {
    if (_context->keyMatch(_depth, key)) {
      sub_obj._context = _context;  // inherit the Context
      sub_obj._depth = _depth + 1;
      sub_obj.structure();   // recurse into sub object
    }
  }
}
