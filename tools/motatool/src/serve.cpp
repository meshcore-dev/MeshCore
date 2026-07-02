#include "serve.h"
#include <cstring>
#include <filesystem>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include "util.h"

namespace fs = std::filesystem;

namespace mota {

// ---- Folder: recursive scan + per-file validation -------------------------------------------------
size_t Folder::scan(const std::string& dir, bool recursive,
                    const std::function<void(const std::string&, const std::string&)>& warn) {
  motas_.clear();
  std::error_code ec;
  auto consider = [&](const fs::path& p) {
    if (p.extension() != ".mota") return;                       // skip non-.mota files
    ServedMota sm; sm.path = p.string();
    if (!read_file(sm.path, sm.bytes)) { warn(sm.path, "cannot read"); return; }
    auto probs = verify(sm.bytes);                              // merkle + image_hash + signature
    if (!probs.empty()) {
      std::string why = probs[0];
      for (size_t i = 1; i < probs.size(); i++) why += "; " + probs[i];
      warn(sm.path, why);                                       // warn + exclude (don't sink the rest)
      return;
    }
    parse(sm.bytes, sm.m);                                      // already validated above
    motas_.push_back(std::move(sm));
  };
  if (recursive) {
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
      if (it->is_regular_file(ec)) consider(it->path());
  } else {
    for (auto it = fs::directory_iterator(dir, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec))
      if (it->is_regular_file(ec)) consider(it->path());
  }
  // stable, deterministic catalog order (indices are how the device addresses motas)
  std::sort(motas_.begin(), motas_.end(),
            [](const ServedMota& a, const ServedMota& b) { return a.path < b.path; });
  return motas_.size();
}

// ---- SeederCore (transport-agnostic) --------------------------------------------------------------
std::array<uint8_t, MOTA_DESC_WIRE> SeederCore::describe(const ServedMota& s) {
  std::array<uint8_t, MOTA_DESC_WIRE> w{};
  std::memcpy(w.data(), s.m.merkle_root.data(), 4);             // mid
  wr_u32(w.data() + 4,  s.m.target_id);
  wr_u32(w.data() + 8,  s.m.fw_version);
  w[12] = s.m.codec_id;
  w[13] = s.m.flags;
  wr_u32(w.data() + 14, (uint32_t)s.bytes.size());              // total_size
  wr_u32(w.data() + 18, s.m.leaves_off());
  wr_u32(w.data() + 22, s.m.block_count);
  wr_u32(w.data() + 26, s.m.payload_off());
  wr_u32(w.data() + 30, s.m.payload_size);
  // [34..38) reserved 0
  return w;
}

std::string SeederCore::store_path(const uint8_t mid[4], bool part) const {
  static const char* H = "0123456789abcdef";
  std::string name;
  for (int i = 0; i < 4; i++) { name += H[mid[i] >> 4]; name += H[mid[i] & 0xF]; }
  name += part ? ".mota.part" : ".mota";
  return store_dir_ + "/" + name;
}

bool SeederCore::handle(uint8_t op, const uint8_t* args, size_t arglen,
                        uint8_t& status, std::vector<uint8_t>& payload) const {
  payload.clear();
  status = MS_STATUS_OK;
  if (op == MS_OP_COUNT) {
    payload.push_back((uint8_t)(folder_.count() > 255 ? 255 : folder_.count()));
    return true;
  }
  if (op == MS_OP_DESCRIBE) {
    if (arglen < 1) return false;
    const ServedMota* s = folder_.at(args[0]);
    if (!s) { status = MS_STATUS_ERR; return true; }
    auto w = describe(*s);
    payload.assign(w.begin(), w.end());
    return true;
  }
  if (op == MS_OP_READ) {
    if (arglen < 7) return false;
    const ServedMota* s = folder_.at(args[0]);
    uint32_t off = rd_u32(args + 1);
    uint16_t len = (uint16_t)(args[5] | (args[6] << 8));
    if (!s || (uint64_t)off + len > s->bytes.size()) { status = MS_STATUS_ERR; return true; }
    payload.assign(s->bytes.begin() + off, s->bytes.begin() + off + len);
    return true;
  }
  // --- STORAGE ops: "pull to folder" — capture a .mota the device is fetching off-mesh into <store_dir>.
  //     All keyed by mid[4]; a partial pull is <midhex>.mota.part, published to <midhex>.mota on OP_FIN. ---
  if (op == MS_OP_STAT || op == MS_OP_BEGIN || op == MS_OP_WRITE || op == MS_OP_SREAD || op == MS_OP_FIN) {
    if (store_dir_.empty() || arglen < 4) { status = MS_STATUS_ERR; return true; }
    const std::string part = store_path(args, true), done = store_path(args, false);

    if (op == MS_OP_STAT) {                                      // present + size (completed wins over partial)
      struct stat sx{}; uint8_t present = 0; uint32_t total = 0;
      if (::stat(done.c_str(), &sx) == 0)      { present = 1; total = (uint32_t)sx.st_size; }
      else if (::stat(part.c_str(), &sx) == 0) { present = 1; total = (uint32_t)sx.st_size; }
      payload.push_back(present);
      uint8_t tb[4]; wr_u32(tb, total); payload.insert(payload.end(), tb, tb + 4);
      return true;
    }
    if (op == MS_OP_BEGIN) {                                     // fresh 0xFF-filled partial (start from 0)
      if (arglen < 8) { status = MS_STATUS_ERR; return true; }
      uint32_t total = rd_u32(args + 4);
      int fd = ::open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd < 0) { status = MS_STATUS_ERR; return true; }
      uint8_t ff[4096]; memset(ff, 0xFF, sizeof ff);
      uint32_t left = total; bool ok = true;
      while (left && ok) { uint32_t c = left < sizeof ff ? left : (uint32_t)sizeof ff; ok = ::write(fd, ff, c) == (ssize_t)c; left -= c; }
      ::close(fd);
      status = ok ? MS_STATUS_OK : MS_STATUS_ERR;
      return true;
    }
    if (op == MS_OP_WRITE) {
      if (arglen < 10) { status = MS_STATUS_ERR; return true; }
      uint32_t off = rd_u32(args + 4);
      uint16_t len = (uint16_t)(args[8] | (args[9] << 8));
      if (arglen < (size_t)10 + len) { status = MS_STATUS_ERR; return true; }
      int fd = ::open(part.c_str(), O_WRONLY);
      if (fd < 0) { status = MS_STATUS_ERR; return true; }
      bool ok = ::pwrite(fd, args + 10, len, off) == (ssize_t)len;
      ::close(fd);
      status = ok ? MS_STATUS_OK : MS_STATUS_ERR;
      return true;
    }
    if (op == MS_OP_SREAD) {                                     // read back (resume: recompute missing blocks)
      if (arglen < 10) { status = MS_STATUS_ERR; return true; }
      uint32_t off = rd_u32(args + 4);
      uint16_t len = (uint16_t)(args[8] | (args[9] << 8));
      const std::string src = (::access(part.c_str(), F_OK) == 0) ? part : done;
      int fd = ::open(src.c_str(), O_RDONLY);
      if (fd < 0) { status = MS_STATUS_ERR; return true; }
      payload.resize(len);
      bool ok = ::pread(fd, payload.data(), len, off) == (ssize_t)len;
      ::close(fd);
      if (!ok) { payload.clear(); status = MS_STATUS_ERR; }
      return true;
    }
    if (op == MS_OP_FIN) {                                       // light-validate (MAGIC + size) then publish
      struct stat sx{};
      if (::stat(part.c_str(), &sx) != 0) { status = MS_STATUS_ERR; return true; }
      uint8_t hd[8] = {0}; int fd = ::open(part.c_str(), O_RDONLY);
      bool got = fd >= 0 && ::read(fd, hd, 8) == 8;
      if (fd >= 0) ::close(fd);
      bool magic = got && hd[0] == 'm' && hd[1] == 'O' && hd[2] == 'T' && hd[3] == 'A';
      if (!magic || rd_u32(hd + 4) != (uint32_t)sx.st_size) { status = MS_STATUS_ERR; return true; }
      std::error_code ec; fs::rename(part, done, ec);
      status = ec ? MS_STATUS_ERR : MS_STATUS_OK;
      return true;
    }
  }
  return false;                                                 // unknown op -> ignore (device retries)
}

// ---- SerialTransport ------------------------------------------------------------------------------
static speed_t baud_const(int b) {
  switch (b) {
    case 9600: return B9600; case 19200: return B19200; case 38400: return B38400;
    case 57600: return B57600; case 115200: return B115200; case 230400: return B230400;
    case 460800: return B460800; case 921600: return B921600; default: return B115200;
  }
}

std::string SerialTransport::open(const std::string& dev, int baud) {
  fd_ = ::open(dev.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) return "cannot open serial device: " + dev;
  termios t{};
  if (tcgetattr(fd_, &t) != 0) { ::close(fd_); fd_ = -1; return "tcgetattr failed"; }
  cfmakeraw(&t);
  speed_t s = baud_const(baud);
  cfsetispeed(&t, s); cfsetospeed(&t, s);
  t.c_cflag |= (CLOCAL | CREAD);
  t.c_cflag &= ~CRTSCTS;                                        // no flow control (matches the daemon)
  t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
  if (tcsetattr(fd_, TCSANOW, &t) != 0) { ::close(fd_); fd_ = -1; return "tcsetattr failed"; }
  return "";
}

SerialTransport::~SerialTransport() { if (fd_ >= 0) ::close(fd_); }

int SerialTransport::read_byte(int timeout_ms) {
  if (fd_ < 0) return -1;
  fd_set rs; FD_ZERO(&rs); FD_SET(fd_, &rs);
  timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
  int r = select(fd_ + 1, &rs, nullptr, nullptr, &tv);
  if (r <= 0) return -1;
  uint8_t b;
  ssize_t n = ::read(fd_, &b, 1);
  return n == 1 ? (int)b : -1;
}

bool SerialTransport::write(const uint8_t* p, size_t n) {
  if (fd_ < 0) return false;
  size_t off = 0;
  while (off < n) {
    ssize_t w = ::write(fd_, p + off, n - off);
    if (w < 0) return false;
    off += (size_t)w;
  }
  return true;
}

// ---- TcpTransport ---------------------------------------------------------------------------------
std::string TcpTransport::open(const std::string& host, int port) {
  addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  std::string portstr = std::to_string(port);
  if (getaddrinfo(host.c_str(), portstr.c_str(), &hints, &res) != 0 || !res)
    return "cannot resolve host: " + host;
  std::string err = "cannot connect to " + host + ":" + portstr;
  for (addrinfo* p = res; p; p = p->ai_next) {
    int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) { fd_ = fd; err.clear(); break; }
    ::close(fd);
  }
  freeaddrinfo(res);
  return err;
}

TcpTransport::~TcpTransport() { if (fd_ >= 0) ::close(fd_); }

int TcpTransport::read_byte(int timeout_ms) {
  if (fd_ < 0) return -1;
  fd_set rs; FD_ZERO(&rs); FD_SET(fd_, &rs);
  timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
  if (select(fd_ + 1, &rs, nullptr, nullptr, &tv) <= 0) return -1;
  uint8_t b;
  ssize_t n = ::recv(fd_, &b, 1, 0);
  return n == 1 ? (int)b : -1;                                  // 0 == peer closed -> treated as timeout/closed
}

bool TcpTransport::write(const uint8_t* p, size_t n) {
  if (fd_ < 0) return false;
  size_t off = 0;
  while (off < n) {
    ssize_t w = ::send(fd_, p + off, n - off, 0);
    if (w < 0) return false;
    off += (size_t)w;
  }
  return true;
}

// ---- seeder framing loop --------------------------------------------------------------------------
static uint8_t xor_bytes(const uint8_t* p, size_t n, uint8_t seed = 0) {
  uint8_t x = seed; for (size_t i = 0; i < n; i++) x ^= p[i]; return x;
}

static bool read_exact(Transport& t, uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    int b = t.read_byte(500);
    if (b < 0) return false;
    buf[i] = (uint8_t)b;
  }
  return true;
}

static void send_response(Transport& t, uint8_t op, uint8_t status, const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> frame;
  frame.reserve(4 + payload.size() + 1);
  frame.push_back(MS_RSP_MAGIC0); frame.push_back(MS_RSP_MAGIC1);
  frame.push_back(op); frame.push_back(status);
  frame.insert(frame.end(), payload.begin(), payload.end());
  frame.push_back(xor_bytes(frame.data(), frame.size()));       // xsum over all prior bytes (incl. magic)
  t.write(frame.data(), frame.size());
}

void serve_loop(Transport& t, const SeederCore& core, bool verbose,
                const std::function<void(const std::string&)>& devline,
                const volatile bool* stop) {
  std::string line;
  int prev = -1;
  auto flush_line = [&]() {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (!line.empty() && devline) devline(line);
    line.clear();
  };
  while (!stop || !*stop) {
    int b = t.read_byte(200);
    if (b < 0) continue;
    if (prev == MS_REQ_MAGIC0 && b == MS_REQ_MAGIC1) {          // 'M''S' -> a request follows
      prev = -1;
      uint8_t op;
      if (!read_exact(t, &op, 1)) continue;
      // fixed header bytes per op; OP_WRITE additionally reads `len` data bytes after its 10-byte header
      size_t hdr = (op == MS_OP_COUNT) ? 0 : (op == MS_OP_DESCRIBE) ? 1 : (op == MS_OP_READ) ? 7
                 : (op == MS_OP_STAT || op == MS_OP_FIN) ? 4 : (op == MS_OP_BEGIN) ? 8
                 : (op == MS_OP_SREAD || op == MS_OP_WRITE) ? 10 : SIZE_MAX;
      if (hdr == SIZE_MAX) continue;                            // unknown op
      uint8_t args[10 + MOTA_SEEDER_WRITE_MAX];
      if (hdr && !read_exact(t, args, hdr)) continue;
      size_t arglen = hdr;
      if (op == MS_OP_WRITE) {                                  // variable payload: header(10) + data(len)
        uint16_t dlen = (uint16_t)(args[8] | (args[9] << 8));
        if (dlen > MOTA_SEEDER_WRITE_MAX) continue;             // guard a runaway frame
        if (dlen && !read_exact(t, args + 10, dlen)) continue;
        arglen = (size_t)10 + dlen;
      }
      uint8_t xs;
      if (!read_exact(t, &xs, 1)) continue;
      if (xs != xor_bytes(args, arglen, op)) continue;          // bad checksum -> ignore; device retries
      uint8_t status; std::vector<uint8_t> payload;
      if (!core.handle(op, args, arglen, status, payload)) continue;
      send_response(t, op, status, payload);
      if (verbose) {
        if (op == MS_OP_COUNT)         devline("COUNT -> " + std::to_string(payload.empty() ? 0 : payload[0]));
        else if (op == MS_OP_DESCRIBE) devline("DESCRIBE " + std::to_string(args[0]) + (status ? " ERR" : " OK"));
        else if (op == MS_OP_READ)     devline("READ " + std::to_string(args[0]) + " @" +
                                               std::to_string(rd_u32(args + 1)) + (status ? " ERR" : " OK"));
      }
      continue;
    }
    if (prev >= 0) {                                            // confirmed device text (not a frame start)
      line.push_back((char)prev);
      if (prev == '\n') flush_line();
      if (line.size() > 512) flush_line();                      // guard runaway lines
    }
    prev = b;
  }
}

} // namespace mota
