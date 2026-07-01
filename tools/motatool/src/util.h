// Small host-side helpers: file I/O, subprocess exec (no shell), hex. Header-only.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace mota {

inline bool read_file(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return (bool)f || f.eof();
}

inline bool write_file(const std::string& path, const uint8_t* data, size_t len) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write((const char*)data, (std::streamsize)len);
  return (bool)f;
}

// Run argv[0] with argv (no shell, so no quoting pitfalls). Returns the child exit code, or -1 to spawn.
// `quiet` redirects the child's stdout to /dev/null (its chatter), keeping stderr so errors still show.
inline int run_argv(const std::vector<std::string>& argv, bool quiet = false) {
  if (argv.empty()) return -1;
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    if (quiet) {
      int n = ::open("/dev/null", O_WRONLY);
      if (n >= 0) { dup2(n, 1); if (n > 2) ::close(n); }
    }
    std::vector<char*> a;
    for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    execvp(a[0], a.data());
    _exit(127);                       // exec failed
  }
  int st = 0;
  if (waitpid(pid, &st, 0) < 0) return -1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

inline std::string to_hex(const uint8_t* p, size_t n) {
  static const char* H = "0123456789ABCDEF";
  std::string s; s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s.push_back(H[p[i] >> 4]); s.push_back(H[p[i] & 0xF]); }
  return s;
}

// Parse a hex string (optionally 0x-prefixed) into bytes. Returns false on odd length / bad char.
inline bool from_hex(const std::string& in, std::vector<uint8_t>& out) {
  std::string s = in;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
  if (s.size() % 2) return false;
  out.clear();
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < s.size(); i += 2) {
    int hi = nib(s[i]), lo = nib(s[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.push_back((uint8_t)((hi << 4) | lo));
  }
  return true;
}

} // namespace mota
