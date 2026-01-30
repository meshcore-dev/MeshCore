#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Mesh.h>

#define ADV_TYPE_NONE         0
#define ADV_TYPE_CHAT         1
#define ADV_TYPE_REPEATER     2
#define ADV_TYPE_ROOM         3
#define ADV_TYPE_SENSOR       4
//FUTURE: 5..15

#define ADV_LATLON_MASK       0x10
#define ADV_FEAT1_MASK        0x20   // FUTURE
#define ADV_FEAT2_MASK        0x40   // FUTURE
#define ADV_NAME_MASK         0x80

// FEAT1 bit flags (encoded when ADV_FEAT1_MASK is set)
#define ADV_FEAT1_ASCON_CAPABLE 0x0001

class AdvertDataBuilder {
  uint8_t _type;
  bool _has_loc;
  const char* _name;
  int32_t _lat, _lon;
  uint16_t _extra1 = 0;
  uint16_t _extra2 = 0;
public:
  AdvertDataBuilder(uint8_t adv_type) : _type(adv_type), _name(NULL), _has_loc(false) { }
  AdvertDataBuilder(uint8_t adv_type, const char* name) : _type(adv_type), _name(name), _has_loc(false) { }
  AdvertDataBuilder(uint8_t adv_type, const char* name, double lat, double lon) : 
      _type(adv_type), _name(name), _has_loc(true), _lat(lat * 1E6), _lon(lon * 1E6)  { }

  void setFeat1(uint16_t extra) { _extra1 = extra; }
  void setFeat2(uint16_t extra) { _extra2 = extra; }

  /**
   * \brief  encode the given advertisement data.
   * \param app_data  dest array, must be MAX_ADVERT_DATA_SIZE
   * \returns  the encoded length in bytes
   */
  uint8_t encodeTo(uint8_t app_data[]);
};

class AdvertDataParser {
  uint8_t _flags;
  bool _valid;
  char _name[MAX_ADVERT_DATA_SIZE];
  int32_t _lat, _lon;
  uint16_t _extra1;
  uint16_t _extra2;
public:
  AdvertDataParser(const uint8_t app_data[], uint8_t app_data_len);

  bool isValid() const { return _valid; }
  uint8_t getType() const { return _flags & 0x0F; }
  uint16_t getFeat1() const { return _extra1; }
  uint16_t getFeat2() const { return _extra2; }

  bool hasName() const { return _name[0] != 0; }
  const char* getName() const { return _name; }

  bool hasLatLon() const { return (_flags & ADV_LATLON_MASK) != 0; }
  int32_t getIntLat() const { return _lat; }
  int32_t getIntLon() const { return _lon; }
  double getLat() const { return ((double)_lat) / 1000000.0; }
  double getLon() const { return ((double)_lon) / 1000000.0; }
};

class AdvertTimeHelper {
public:
  static void formatRelativeTimeDiff(char dest[], int32_t seconds_from_now, bool short_fmt);
};
