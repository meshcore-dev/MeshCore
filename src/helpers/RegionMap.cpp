#include "RegionMap.h"
#include <helpers/TxtDataHelpers.h>
#include <SHA256.h>

RegionMap::RegionMap(TransportKeyStore& store) : _store(&store) {
  next_id = 1; num_regions = 0; home_id = 0;
  wildcard.id = wildcard.parent = 0;
  wildcard.flags = 0;  // default behaviour, allow flood and direct
  strcpy(wildcard.name, "*");
}

bool RegionMap::is_name_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '#';
}

static File openWrite(FILESYSTEM* _fs, const char* filename) {
  #if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    _fs->remove(filename);
    return _fs->open(filename, FILE_O_WRITE);
  #elif defined(RP2040_PLATFORM)
    return _fs->open(filename, "w");
  #else
    return _fs->open(filename, "w", true);
  #endif
}

bool RegionMap::load(FILESYSTEM* _fs) {
  if (_fs->exists("/regions2")) {
  #if defined(RP2040_PLATFORM)
    File file = _fs->open("/regions2", "r");
  #else
    File file = _fs->open("/regions2");
  #endif

    if (file) {
      uint8_t pad[128];

      num_regions = 0; next_id = 1; home_id = 0;

      bool success = file.read(pad, 5) == 5;  // reserved header
      success = success && file.read((uint8_t *) &home_id, sizeof(home_id)) == sizeof(home_id);
      success = success && file.read((uint8_t *) &wildcard.flags, sizeof(wildcard.flags)) == sizeof(wildcard.flags);
      success = success && file.read((uint8_t *) &next_id, sizeof(next_id)) == sizeof(next_id);

      if (success) {
        while (num_regions < MAX_REGION_ENTRIES) {
          auto r = &regions[num_regions];

          success = file.read((uint8_t *) &r->id, sizeof(r->id)) == sizeof(r->id);
          success = success && file.read((uint8_t *) &r->parent, sizeof(r->parent)) == sizeof(r->parent);
          success = success && file.read((uint8_t *) r->name, sizeof(r->name)) == sizeof(r->name);
          success = success && file.read((uint8_t *) &r->flags, sizeof(r->flags)) == sizeof(r->flags);
          success = success && file.read(pad, sizeof(pad)) == sizeof(pad);

          if (!success) break; // EOF

          if (r->id >= next_id) {    // make sure next_id is valid
            next_id = r->id + 1;
          }
          num_regions++;
        }
      }
      file.close();
      return true;
    }
  }
  return false;  // failed
}

bool RegionMap::save(FILESYSTEM* _fs) {
  File file = openWrite(_fs, "/regions2");
  if (file) {
    uint8_t pad[128];
    memset(pad, 0, sizeof(pad));

    bool success = file.write(pad, 5) == 5;  // reserved header
    success = success && file.write((uint8_t *) &home_id, sizeof(home_id)) == sizeof(home_id);
    success = success && file.write((uint8_t *) &wildcard.flags, sizeof(wildcard.flags)) == sizeof(wildcard.flags);
    success = success && file.write((uint8_t *) &next_id, sizeof(next_id)) == sizeof(next_id);

    if (success) {
      for (int i = 0; i < num_regions; i++) {
        auto r = &regions[i];

        success = file.write((uint8_t *) &r->id, sizeof(r->id)) == sizeof(r->id);
        success = success && file.write((uint8_t *) &r->parent, sizeof(r->parent)) == sizeof(r->parent);
        success = success && file.write((uint8_t *) r->name, sizeof(r->name)) == sizeof(r->name);
        success = success && file.write((uint8_t *) &r->flags, sizeof(r->flags)) == sizeof(r->flags);
        success = success && file.write(pad, sizeof(pad)) == sizeof(pad);
        if (!success) break; // write failed
      }
    }
    file.close();
    return true;
  }
  return false;  // failed
}

RegionEntry* RegionMap::putRegion(const char* name, uint16_t parent_id, uint16_t id) {
  const char* sp = name;  // check for illegal name chars
  while (*sp) {
    if (!is_name_char(*sp)) return NULL;   // error
    sp++;
  }

  auto region = findByName(name);
  if (region) {
    if (region->id == parent_id) return NULL;   // ERROR: invalid parent!

    region->parent = parent_id;   // re-parent / move this region in the hierarchy
  } else {
    if (id == 0 && num_regions >= MAX_REGION_ENTRIES) return NULL;  // full!

    region = &regions[num_regions++];   // alloc new RegionEntry
    region->flags = REGION_DENY_FLOOD;     // DENY by default
    region->id = id == 0 ? next_id++ : id;
    StrHelper::strncpy(region->name, name, sizeof(region->name));
    region->parent = parent_id;
  }
  return region;
}

RegionEntry* RegionMap::findMatch(mesh::Packet* packet, uint8_t mask) {
  for (int i = 0; i < num_regions; i++) {
    auto region = &regions[i];
    if ((region->flags & mask) == 0) {   // does region allow this? (per 'mask' param)
      TransportKey keys[4];
      int num;
      if (region->name[0] == '#') {   // auto hashtag region
        _store->getAutoKeyFor(region->id, region->name, keys[0]);
        num = 1;
      } else {
        num = _store->loadKeysFor(region->id, keys, 4);
      }
      for (int j = 0; j < num; j++) {
        uint16_t code = keys[j].calcTransportCode(packet);
        if (packet->transport_codes[0] == code) {   // a match!!
          return region;
        }
      }
    }
  }
  return NULL;  // no matches
}

RegionEntry* RegionMap::findByName(const char* name) {
  if (strcmp(name, "*") == 0) return &wildcard;

  for (int i = 0; i < num_regions; i++) {
    auto region = &regions[i];
    if (strcmp(name, region->name) == 0) return region;
  }
  return NULL;  // not found
}

RegionEntry* RegionMap::findByNamePrefix(const char* prefix) {
  if (strcmp(prefix, "*") == 0) return &wildcard;

  RegionEntry* partial = NULL;
  for (int i = 0; i < num_regions; i++) {
    auto region = &regions[i];
    if (strcmp(prefix, region->name) == 0) return region;  // is a complete match, preference this one
    if (memcmp(prefix, region->name, strlen(prefix)) == 0) {
      partial = region;
    }
  }
  return partial;
}

RegionEntry* RegionMap::findById(uint16_t id) {
  if (id == 0) return &wildcard;   // special root Region

  for (int i = 0; i < num_regions; i++) {
    auto region = &regions[i];
    if (region->id == id) return region;
  }
  return NULL;  // not found
}

RegionEntry* RegionMap::getHomeRegion() {
  return findById(home_id);
}

void RegionMap::setHomeRegion(const RegionEntry* home) {
  home_id = home ? home->id : 0;
}

bool RegionMap::removeRegion(const RegionEntry& region) {
  if (region.id == 0) return false;  // failed (cannot remove the wildcard Region)

  int i;     // first check region has no child regions
  for (i = 0; i < num_regions; i++) {
    if (regions[i].parent == region.id) return false;   // failed (must remove child Regions first)
  }

  i = 0;
  while (i < num_regions) {
    if (region.id == regions[i].id) break;
    i++;
  }
  if (i >= num_regions) return false;  // failed (not found)

  num_regions--;    // remove from regions array
  while (i < num_regions) {
    regions[i] = regions[i + 1];
    i++;
  }
  return true;  // success
}

bool RegionMap::clear() {
  num_regions = 0;
  return true;  // success
}

void RegionMap::printChildRegions(int indent, const RegionEntry* parent, Stream& out) const {
  for (int i = 0; i < indent; i++) {
    out.print(' ');
  }

  if (parent->flags & REGION_DENY_FLOOD) {
    out.printf("%s%s\n", parent->name, parent->id == home_id ? "^" : "");
  } else {
    out.printf("%s%s F\n", parent->name, parent->id == home_id ? "^" : "");
  }

  for (int i = 0; i < num_regions; i++) {
    auto r = &regions[i];
    if (r->parent == parent->id) {
      printChildRegions(indent + 1, r, out);
    }
  }
}

void RegionMap::exportTo(Stream& out) const {
  printChildRegions(0, &wildcard, out);   // recursive
}
