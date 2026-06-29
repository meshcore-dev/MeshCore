#include "input.h"
#include "util.h"
#include <cctype>
#include <cstring>
#include <unistd.h>

namespace mota {

bool is_url(const std::string& s) {
  return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

static bool ends_with_ci(const std::string& s, const std::string& suf) {
  if (s.size() < suf.size()) return false;
  for (size_t i = 0; i < suf.size(); i++)
    if (std::tolower((unsigned char)s[s.size() - suf.size() + i]) != std::tolower((unsigned char)suf[i])) return false;
  return true;
}

// Parse Intel HEX text into the flat binary it represents (min..max address, gaps filled 0xFF).
// nRF52/STM32 PlatformIO builds emit firmware.hex; pio_endf appends the EndF inside it, so the extracted
// binary IS the OTA image (BODY||EndF) starting at the app base — exactly what `build` wants.
static std::string parse_intel_hex(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
  const std::string s((const char*)in.data(), in.size());
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  struct Seg { uint64_t addr; std::vector<uint8_t> data; };
  std::vector<Seg> segs;
  uint64_t base = 0, lo = UINT64_MAX, hi = 0;
  size_t i = 0;
  bool saw_eof = false;
  while (i < s.size()) {
    if (s[i] != ':') { i++; continue; }       // tolerate CR/LF/whitespace between records
    size_t p = i + 1;
    auto rb = [&](uint8_t& v) -> bool {
      if (p + 2 > s.size()) return false;
      int h = nib(s[p]), l = nib(s[p + 1]);
      if (h < 0 || l < 0) return false;
      v = (uint8_t)((h << 4) | l); p += 2; return true;
    };
    uint8_t len, ah, al, type;
    if (!rb(len) || !rb(ah) || !rb(al) || !rb(type)) return "malformed Intel HEX record";
    uint8_t sum = (uint8_t)(len + ah + al + type);
    std::vector<uint8_t> data(len);
    for (uint8_t k = 0; k < len; k++) { if (!rb(data[k])) return "truncated Intel HEX data"; sum += data[k]; }
    uint8_t cks;
    if (!rb(cks)) return "missing Intel HEX checksum";
    if ((uint8_t)(sum + cks) != 0) return "Intel HEX checksum error";
    uint16_t addr = (uint16_t)((ah << 8) | al);
    if (type == 0x00) {                        // data
      uint64_t a = base + addr;
      if (a < lo) lo = a;
      if (a + len > hi) hi = a + len;
      segs.push_back({a, std::move(data)});
    } else if (type == 0x04) {                 // extended linear address (upper 16 bits)
      if (len != 2) return "bad Intel HEX ELA record";
      base = (uint64_t)((data[0] << 8) | data[1]) << 16;
    } else if (type == 0x02) {                 // extended segment address
      if (len != 2) return "bad Intel HEX ESA record";
      base = (uint64_t)((data[0] << 8) | data[1]) << 4;
    } else if (type == 0x01) {                 // EOF
      saw_eof = true; break;
    }                                          // 0x03/0x05 (start address) ignored
    i = p;
  }
  if (segs.empty()) return "no data records in Intel HEX";
  if (!saw_eof) return "Intel HEX missing EOF record";
  out.assign((size_t)(hi - lo), 0xFF);
  for (auto& sg : segs) std::memcpy(out.data() + (size_t)(sg.addr - lo), sg.data.data(), sg.data.size());
  return "";
}

std::string read_input(const std::string& src, std::vector<uint8_t>& out) {
  std::vector<uint8_t> raw;
  if (!is_url(src)) {
    if (!read_file(src, raw)) return "cannot read file: " + src;
  } else {
    // URL: download to a temp file via curl (fallback wget), then read it back.
    char tmp[] = "/tmp/motadlXXXXXX";
    int fd = mkstemp(tmp);
    if (fd < 0) return "mkstemp failed";
    close(fd);
    int rc = run_argv({"curl", "-fLsS", "-o", tmp, src});   // -f fail, -L follow, -sS silent+show-errors
    if (rc == 127) rc = run_argv({"wget", "-q", "-O", tmp, src});
    std::string err;
    if (rc == 127) err = "neither curl nor wget is available to fetch " + src;
    else if (rc != 0) err = "download failed (exit " + std::to_string(rc) + "): " + src;
    else if (!read_file(tmp, raw)) err = "could not read the downloaded file: " + src;
    unlink(tmp);
    if (!err.empty()) return err;
  }
  if (raw.empty()) return "input is empty: " + src;
  if (ends_with_ci(src, ".hex")) return parse_intel_hex(raw, out);   // Intel HEX -> flat image
  out = std::move(raw);
  return "";
}

} // namespace mota
