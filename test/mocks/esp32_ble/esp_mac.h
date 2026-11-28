#pragma once
#include <cstdint>
#include <cstring>
inline void esp_efuse_mac_get_default(uint8_t* addr) { memset(addr, 0, 6); }
