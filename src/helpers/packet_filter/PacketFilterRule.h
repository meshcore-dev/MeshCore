#pragma once

#include "AdvertRateLimitClassifier.h"
#include "ChannelMessageClassifier.h"

#include <new>
#include <string.h>

class PacketFilterRule {
public:
  enum Type {
    RULE_NONE,
    RULE_CHANNEL_MESSAGE,
    RULE_ADVERT
  };

private:
  Type _type;

  union Classifier {
    ChannelMessageClassifier channel_message;
    AdvertRateLimitClassifier advert;

    Classifier() {}
    ~Classifier() {}
  } _classifier;

public:
  PacketFilterRule() : _type(RULE_NONE) {}
  ~PacketFilterRule() { clear(); }

  void clear() {
    switch (_type) {
    case RULE_CHANNEL_MESSAGE:
      _classifier.channel_message.~ChannelMessageClassifier();
      break;
    case RULE_ADVERT:
      _classifier.advert.~AdvertRateLimitClassifier();
      break;
    default:
      break;
    }
    _type = RULE_NONE;
  }

  bool configure(const char* classifier_name, char* args, const mesh::Identity* self = NULL, mesh::RNG* rng = NULL) {
    (void)self;
    if (classifier_name == NULL) return false;

    if (strcmp(classifier_name, "channelmessage") == 0) {
      ChannelMessageClassifier next;
      if (!next.configure(args)) return false;
      clear();
      new (&_classifier.channel_message) ChannelMessageClassifier(next);
      _type = RULE_CHANNEL_MESSAGE;
      return true;
    } else if (strcmp(classifier_name, "advert") == 0) {
      AdvertRateLimitClassifier next(rng);
      if (!next.configure(args)) return false;
      clear();
      new (&_classifier.advert) AdvertRateLimitClassifier(next);
      _type = RULE_ADVERT;
      return true;
    }

    return false;
  }

  bool isActive() const { return _type != RULE_NONE; }
  Type getType() const { return _type; }

  void formatRule(char* dest, size_t dest_len) const {
    switch (_type) {
    case RULE_CHANNEL_MESSAGE:
      _classifier.channel_message.formatRule(dest, dest_len);
      break;
    case RULE_ADVERT:
      _classifier.advert.formatRule(dest, dest_len);
      break;
    default:
      if (dest_len > 0) dest[0] = 0;
      break;
    }
  }

  bool shouldRepeatPacket(const mesh::Packet* packet) {
    switch (_type) {
    case RULE_CHANNEL_MESSAGE:
      return _classifier.channel_message.shouldRepeatPacket(packet);
    case RULE_ADVERT:
      return _classifier.advert.shouldRepeatPacket(packet);
    default:
      return true;
    }
  }
};
