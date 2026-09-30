#pragma once
#include <stdint.h>

// Kestrel PLI data type — used in PAYLOAD_TYPE_GRP_DATA packets
// Registered data_type value for Kestrel tracker nodes
// 'KE'strel PLI: 0x0E51
#define KESTREL_PLI_DATA_TYPE_U16  ((uint16_t)0x0E51)

// Fix quality flags
#define KESTREL_FIX_NONE    0x00
#define KESTREL_FIX_2D      0x01
#define KESTREL_FIX_3D      0x02

// Node role flags (matches ATAK role codes)
#define KESTREL_ROLE_TEAM_MEMBER   0x00
#define KESTREL_ROLE_TEAM_LEAD     0x01
#define KESTREL_ROLE_CASEVAC       0x02   // casualty being tracked
#define KESTREL_ROLE_ASSET         0x03   // equipment/vehicle

#pragma pack(push, 1)
struct KestrelPLIPacket {
    uint16_t data_type;       // must be KESTREL_PLI_DATA_TYPE_U16
    int32_t  lat_e7;          // latitude * 1e7 (WGS84)
    int32_t  lon_e7;          // longitude * 1e7 (WGS84)
    int16_t  alt_m;           // altitude metres HAE
    uint8_t  battery_pct;     // 0-100
    uint8_t  fix_quality;     // KESTREL_FIX_*
    uint8_t  role;            // KESTREL_ROLE_*
    uint8_t  callsign_len;    // length of callsign string following
    char     callsign[16];    // callsign, not null-terminated (use callsign_len)
};
#pragma pack(pop)

#define KESTREL_PLI_PACKET_SIZE  sizeof(KestrelPLIPacket)
