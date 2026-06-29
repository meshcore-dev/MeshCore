// Serve a folder of .mota to a MeshCore node. Split into a transport-AGNOSTIC protocol core (reusable
// over USB-serial today and BLE/GATT in the future) and a serial byte-stream framing layer.
#pragma once
#include "mota.h"
#include "mota_format.h"
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace mota {

struct ServedMota {
  std::string          path;
  std::vector<uint8_t> bytes;
  Manifest             m;
};

// Recursively collect every *.mota under `dir`, validate each, keep only the valid ones. Corrupt/invalid
// files are reported through `warn(path, reason)` and excluded — one bad file never sinks the rest.
class Folder {
public:
  size_t scan(const std::string& dir, bool recursive,
              const std::function<void(const std::string&, const std::string&)>& warn);
  size_t count() const { return motas_.size(); }
  const ServedMota* at(size_t i) const { return i < motas_.size() ? &motas_[i] : nullptr; }
  const std::vector<ServedMota>& all() const { return motas_; }
private:
  std::vector<ServedMota> motas_;
};

// Transport-agnostic seeder: turns a (op, args) request into a (status, payload) response. The BLE path
// would call this directly from a characteristic-write handler and notify the reply — no framing needed.
class SeederCore {
public:
  explicit SeederCore(const Folder& f) : folder_(f) {}
  bool handle(uint8_t op, const uint8_t* args, size_t arglen,
              uint8_t& status, std::vector<uint8_t>& payload) const;
  static std::array<uint8_t, MOTA_DESC_WIRE> describe(const ServedMota& s);
private:
  const Folder& folder_;
};

// A bidirectional byte link (the serial seeder needs this; BLE would reuse SeederCore directly).
struct Transport {
  virtual ~Transport() {}
  virtual int  read_byte(int timeout_ms) = 0;          // a byte 0..255, or -1 on timeout/closed
  virtual bool write(const uint8_t* p, size_t n) = 0;
  bool write_str(const std::string& s) { return write((const uint8_t*)s.data(), s.size()); }
};

class SerialTransport : public Transport {
public:
  std::string open(const std::string& dev, int baud);   // "" on success
  ~SerialTransport() override;
  int  read_byte(int timeout_ms) override;
  bool write(const uint8_t* p, size_t n) override;
private:
  int fd_ = -1;
};

// Serial framing loop: resync on 'M''S', verify the request checksum, dispatch to `core`, frame the
// reply. Device text/log lines sharing the wire are surfaced via `devline`. Runs until *stop becomes true.
void serve_serial(Transport& t, const SeederCore& core, bool verbose,
                  const std::function<void(const std::string&)>& devline,
                  const volatile bool* stop);

} // namespace mota
