#include "FirmwareIdentity.h"

#include "helpers/ota/OtaFormat.h"

namespace mesh {
namespace {

using ota::FwVersion;

uint32_t firmware_version_packed() {
  const char* s = firmware_version_string();
  if (!s) return 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9') continue;
    const char* p = s;
    uint32_t a = 0, b = 0, d = 0;
    int dots = 0;
    uint32_t* cur = &a;
    for (; *p; p++) {
      if (*p >= '0' && *p <= '9') {
        *cur = *cur * 10 + (uint32_t)(*p - '0');
      } else if (*p == '.' && dots < 2) {
        dots++;
        cur = (dots == 1) ? &b : &d;
      } else {
        break;
      }
    }
    if (dots >= 1) return FwVersion{(uint8_t)a, (uint8_t)b, (uint8_t)d, 0}.pack();
    s = p - 1;
  }
  return 0;
}

}  // namespace

}  // namespace mesh
