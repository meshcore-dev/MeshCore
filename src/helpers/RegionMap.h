#pragma once

#include <Arduino.h>   // needed for PlatformIO
#include <Packet.h>
#include "TransportKeyStore.h"

#ifndef MAX_REGION_ENTRIES
  #define MAX_REGION_ENTRIES  32
#endif

#define REGION_DENY_FLOOD   0x01
#define REGION_DENY_DIRECT  0x02   // reserved for future

struct RegionEntry {
  uint16_t id;
  uint16_t parent;
  uint8_t flags;
  char name[31];
};

class RegionMap {
  TransportKeyStore* _store;
  uint16_t next_id, home_id;
  uint16_t num_regions;
  RegionEntry regions[MAX_REGION_ENTRIES];
  RegionEntry wildcard;

  void printChildRegions(int indent, const RegionEntry* parent, Stream& out) const;

public:
  RegionMap(TransportKeyStore& store);

  static bool is_name_char(uint8_t c);

  bool load(FILESYSTEM* _fs, const char* path=NULL);
  bool save(FILESYSTEM* _fs, const char* path=NULL);

  RegionEntry* putRegion(const char* name, uint16_t parent_id, uint16_t id = 0);
  RegionEntry* findMatch(mesh::Packet* packet, uint8_t mask);
  RegionEntry& getWildcard() { return wildcard; }
  RegionEntry* findByName(const char* name);
  RegionEntry* findByNamePrefix(const char* prefix);
  RegionEntry* findById(uint16_t id);
  RegionEntry* getHomeRegion();   // NOTE: can be NULL
  void setHomeRegion(const RegionEntry* home);
  bool removeRegion(const RegionEntry& region);
  bool clear();
  void resetFrom(const RegionMap& src) { num_regions = 0; next_id = src.next_id; }
  int getCount() const { return num_regions; }
  const RegionEntry* getByIdx(int i) const { return &regions[i]; }
  const RegionEntry* getRoot() const { return &wildcard; }
  int exportNamesTo(char *dest, int max_len, uint8_t mask, bool invert = false);

  void    exportTo(Stream& out) const;
  size_t  exportTo(char *dest, size_t max_len) const;
 
};
