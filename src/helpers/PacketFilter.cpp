#include "PacketFilter.h"

#include <stdlib.h>

static File openPacketFilterRead(FILESYSTEM* fs, const char* path) {
#if defined(RP2040_PLATFORM)
  return fs->open(path, "r");
#else
  return fs->open(path);
#endif
}

static File openPacketFilterWrite(FILESYSTEM* fs, const char* path) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(path);
  return fs->open(path, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(path, "w");
#else
  return fs->open(path, "w", true);
#endif
}

static bool appendRuleLine(char* reply, size_t reply_len, size_t* used, const char* line) {
  int written = snprintf(&reply[*used], reply_len - *used, "%s%s", *used == 0 ? "" : "\n", line);
  if (written < 0) return false;
  if ((size_t)written >= reply_len - *used) {
    reply[reply_len - 1] = 0;
    return false;
  }
  *used += written;
  return true;
}

static bool canAppendPagedLine(size_t used, size_t reply_len, const char* line) {
  const size_t continuation_reserve = 8; // "\n..255" plus terminator
  size_t line_len = strlen(line);
  size_t needed = line_len + (used == 0 ? 0 : 1);

  return reply_len > continuation_reserve && needed < reply_len - continuation_reserve - used;
}

static void consumePagedLine(size_t* used, const char* line) {
  *used += strlen(line) + (*used == 0 ? 0 : 1);
}

static bool appendContinuation(char* reply, size_t reply_len, size_t* used, uint8_t next_page) {
  char line[8];
  snprintf(line, sizeof(line), "..%u", next_page);
  return appendRuleLine(reply, reply_len, used, line);
}

PacketFilter::PacketFilter(mesh::RNG* rng) {
  _rule_count = 0;
  _filtered_count = 0;
  _rng = rng;
}

bool PacketFilter::shouldRepeatPacket(const mesh::Packet* packet) {
  for (uint8_t i = 0; i < PACKET_FILTER_MAX_RULES; i++) {
    if (!_rules[i].shouldRepeatPacket(packet)) {
      _filtered_count++;
      return false;
    }
  }
  return true;
}

bool PacketFilter::block(const char* classifier_name, char* args) {
  int empty_rule_index = -1;
  for (uint8_t i = 0; i < PACKET_FILTER_MAX_RULES; i++) {
    if (!_rules[i].isActive()) {
      empty_rule_index = i;
      break;
    }
  }
  if (empty_rule_index < 0) return false;

  PacketFilterRule& new_rule = _rules[empty_rule_index];
  if (!new_rule.configure(classifier_name, args, nullptr, _rng)) return false;

  char new_rule_text[160];
  char existing_rule_text[160];
  new_rule.formatRule(new_rule_text, sizeof(new_rule_text));

  for (uint8_t i = 0; i < PACKET_FILTER_MAX_RULES; i++) {
    if (i == empty_rule_index || !_rules[i].isActive()) continue;

    _rules[i].formatRule(existing_rule_text, sizeof(existing_rule_text));
    if (strcmp(new_rule_text, existing_rule_text) == 0) {
      new_rule.clear();
      return false;
    }
  }

  _rule_count++;
  return true;
}

bool PacketFilter::deleteRule(uint8_t rule_index) {
  if (rule_index == 0 || rule_index > PACKET_FILTER_MAX_RULES) return false;

  PacketFilterRule& rule = _rules[rule_index - 1];
  if (!rule.isActive()) return false;

  rule.clear();
  if (_rule_count > 0) _rule_count--;
  return true;
}

void PacketFilter::clear() {
  for (uint8_t i = 0; i < PACKET_FILTER_MAX_RULES; i++) {
    _rules[i].clear();
  }
  _rule_count = 0;
}

void PacketFilter::formatRules(char* reply, size_t reply_len, uint8_t page) const {
  if (reply_len == 0) return;
  reply[0] = 0;

  if (page == 0) page = 1;

  size_t used = 0;
  uint8_t current_page = 1;
  bool has_active_rules = false;
  bool has_page_rules = false;
  char rule_text[128];
  char line[144];

  for (uint8_t i = 0; i < PACKET_FILTER_MAX_RULES; i++) {
    if (!_rules[i].isActive()) continue;
    has_active_rules = true;

    _rules[i].formatRule(rule_text, sizeof(rule_text));
    snprintf(line, sizeof(line), "%u: %s", (uint32_t)i + 1, rule_text);

    while (!canAppendPagedLine(used, reply_len, line)) {
      if (current_page == page) {
        appendContinuation(reply, reply_len, &used, current_page + 1);
        return;
      }

      current_page++;
      used = 0;
    }

    if (current_page == page) {
      if (!appendRuleLine(reply, reply_len, &used, line)) return;
      has_page_rules = true;
    } else {
      consumePagedLine(&used, line);
    }
  }

  if (!has_active_rules) {
    strncpy(reply, "-none-", reply_len - 1);
    reply[reply_len - 1] = 0;
  } else if (!has_page_rules) {
    snprintf(reply, reply_len, "Err - no such page");
  }
}

static void formatBlockHelp(char* reply, size_t reply_len) {
  snprintf(reply, reply_len, "block list [page]\nblock del <index>\nblock clear\nblock stats\nblock advert help\nblock channelmessage help");
}

static bool addBlockRule(void* context, const char* classifier_name, char* args) {
  if (context == NULL) return false;
  return ((PacketFilter*)context)->block(classifier_name, args);
}

void PacketFilter::handleBlockCommand(FILESYSTEM* fs, const char* path, char* command, char* reply, size_t reply_len) {
  if (reply_len == 0) return;

  char* args = command + 5;
  char* rule = BasePacketFilterClassifier::nextToken(args);

  bool should_save = false;
  if (rule == NULL || strcmp(rule, "help") == 0) {
    formatBlockHelp(reply, reply_len);
    return;
  } else if (strcmp(rule, "list") == 0) {
    char* page_token = BasePacketFilterClassifier::nextToken(args);
    char* extra = BasePacketFilterClassifier::nextToken(args);
    unsigned long page = 1;
    if (extra != NULL || (page_token != NULL && !BasePacketFilterClassifier::parseUnsigned(page_token, 1, 255, &page))) {
      snprintf(reply, reply_len, "Usage: block list [page]");
      return;
    }
    formatRules(reply, reply_len, (uint8_t)page);
    return;
  } else if (strcmp(rule, "del") == 0) {
    char* index_token = BasePacketFilterClassifier::nextToken(args);
    char* extra = BasePacketFilterClassifier::nextToken(args);
    unsigned long rule_index = 0;
    if (extra != NULL || !BasePacketFilterClassifier::parseUnsigned(index_token, 1, PACKET_FILTER_MAX_RULES, &rule_index)) {
      snprintf(reply, reply_len, "Usage: block del <index>");
      return;
    }
    if (deleteRule((uint8_t)rule_index)) {
      snprintf(reply, reply_len, "OK - packet filter rule deleted");
      should_save = true;
    } else {
      snprintf(reply, reply_len, "Err - packet filter rule not found");
    }
  } else if (strcmp(rule, "clear") == 0) {
    clear();
    snprintf(reply, reply_len, "OK - packet filter rules cleared");
    should_save = true;
  } else if (strcmp(rule, "stats") == 0) {
    snprintf(reply, reply_len, "Filtered packets: %u", _filtered_count);
  } else if (strcmp(rule, "channelmessage") == 0) {
    should_save = ChannelMessageClassifier::handleBlockCommand(args, reply, reply_len, addBlockRule, this);
  } else if (strcmp(rule, "advert") == 0) {
    should_save = AdvertRateLimitClassifier::handleBlockCommand(args, reply, reply_len, addBlockRule, this);
  } else {
    snprintf(reply, reply_len, "Err - unknown block rule");
  }

  if (should_save && !save(fs, path)) {
    snprintf(reply, reply_len, "Err - packet filter rule save failed");
  }
}

static void loadPacketFilterRuleLine(PacketFilter* packet_filter, char* line) {
  char* args = line;
  char* classifier_name = BasePacketFilterClassifier::nextToken(args);
  if (classifier_name == NULL) return;

  packet_filter->block(classifier_name, args);
}

bool PacketFilter::load(FILESYSTEM* fs, const char* path) {
  clear();
  if (fs == NULL || path == NULL || !fs->exists(path)) return true;

  File file = openPacketFilterRead(fs, path);
  if (!file) return false;

  char line[160];
  size_t line_len = 0;

  while (file.available()) {
    int c = file.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[line_len] = 0;
      loadPacketFilterRuleLine(this, line);
      line_len = 0;
    } else if (line_len < sizeof(line) - 1) {
      line[line_len++] = (char)c;
    }
  }

  if (line_len > 0) {
    line[line_len] = 0;
    loadPacketFilterRuleLine(this, line);
  }

  file.close();
  return true;
}

bool PacketFilter::save(FILESYSTEM* fs, const char* path) const {
  if (fs == NULL || path == NULL) return false;

  File file = openPacketFilterWrite(fs, path);
  if (!file) return false;

  char line[160];
  for (uint8_t i = 0; i < PACKET_FILTER_MAX_RULES; i++) {
    if (!_rules[i].isActive()) continue;
    _rules[i].formatRule(line, sizeof(line));
    file.print(line);
    file.print('\n');
  }

  file.close();
  return true;
}
