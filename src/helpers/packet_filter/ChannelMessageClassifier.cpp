#include "helpers/packet_filter/ChannelMessageClassifier.h"

#include <helpers/ChannelDetails.h>
#include <helpers/SimplePatternMatcher.h>
#include <helpers/TxtDataHelpers.h>
#include <Utils.h>

ChannelMessageClassifier::ChannelMessageClassifier() {
  memset(_channel_name, 0, sizeof(_channel_name));
  memset(_pattern, 0, sizeof(_pattern));
  memset(_channel_hash, 0, sizeof(_channel_hash));
  memset(_channel_secret, 0, sizeof(_channel_secret));
}

bool ChannelMessageClassifier::configure(char* args) {
  char* channel_name = nextToken(args);
  char* pattern = unquote(remainingArgs(args));

  if (channel_name == NULL || pattern == NULL || channel_name[0] == 0 || pattern[0] == 0) return false;
  if (strlen(channel_name) >= sizeof(_channel_name) || strlen(pattern) >= sizeof(_pattern)) return false;

  uint8_t secret[PUB_KEY_SIZE];
  if (!mesh::GroupChannel::derivePublicSecret(channel_name, secret)) return false;

  memset(_channel_name, 0, sizeof(_channel_name));
  memset(_pattern, 0, sizeof(_pattern));
  strncpy(_channel_name, channel_name, sizeof(_channel_name) - 1);
  strncpy(_pattern, pattern, sizeof(_pattern) - 1);
  memcpy(_channel_secret, secret, sizeof(_channel_secret));
  mesh::GroupChannel channel;
  memcpy(channel.secret, _channel_secret, sizeof(channel.secret));
  channel.deriveHash();
  memcpy(_channel_hash, channel.hash, sizeof(_channel_hash));
  return true;
}

void ChannelMessageClassifier::formatRule(char* dest, size_t dest_len) const {
  snprintf(dest, dest_len, "channelmessage %s %s", _channel_name, _pattern);
}

bool ChannelMessageClassifier::shouldRepeatPacket(const mesh::Packet* packet) {
  if (packet == NULL || packet->getPayloadType() != PAYLOAD_TYPE_GRP_TXT || packet->payload_len <= 1) return true;
  if (packet->payload[0] != _channel_hash[0]) return true;

  uint8_t data[MAX_PACKET_PAYLOAD + 1];
  int len = mesh::Utils::MACThenDecrypt(_channel_secret, data, &packet->payload[1], packet->payload_len - 1);
  if (len <= 5) return true;

  uint8_t txt_type = data[4] >> 2;
  if (txt_type != TXT_TYPE_PLAIN) return true;

  data[len] = 0;
  const char* text = (const char*)&data[5];
  return !SimplePatternMatcher::matches(_pattern, text);
}

bool ChannelMessageClassifier::handleBlockCommand(char* args, char* reply, size_t reply_len,
                                                  BlockRuleFn block_rule, void* block_context) {
  return BasePacketFilterClassifier::handleBlockCommand(
    "channelmessage",
    args,
    reply,
    reply_len,
    "Usage: block channelmessage #channel \"pattern\"",
    "OK - channel message block added",
    block_rule,
    block_context
  );
}
