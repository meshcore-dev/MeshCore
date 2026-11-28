#pragma once

// Mock of the nRF52 filesystem API for native tests. Only what IdentityStore
// touches is modelled: open, exists, remove, mkdir, and a File that is a
// Stream.
//
// Two failure modes are modelled, because the platforms differ in which one
// they show.
//
// The write budget: a real flash lets a file open and then refuses the write
// once the medium is full or worn out, so a test needs a file that is valid but
// whose writes come up short. Print::write() loops over write(uint8_t), so a
// budget that runs out mid buffer returns a short count, which is how littlefs
// reports it on nRF52 and STM32.
//
// The commit budget: on ESP32 the core opens files fully buffered, so write()
// reports success for bytes that are still in a stdio buffer, and only close()
// hands them over. File::close() is void there and drops the error, which
// leaves a short file behind that no return value ever mentioned. A commit
// budget below the number of bytes written reproduces exactly that, and
// corrupt_on_commit covers the worn cell that keeps the length but changes a
// byte.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "Stream.h"

#define FILE_O_READ   0
#define FILE_O_WRITE  1

namespace Adafruit_LittleFS_Namespace {

struct FileData {
  std::vector<uint8_t> bytes;
  size_t write_budget = std::numeric_limits<size_t>::max();
  size_t commit_budget = std::numeric_limits<size_t>::max();      // what close() keeps
  size_t corrupt_on_commit = std::numeric_limits<size_t>::max();  // index close() flips
};

class File : public Stream {
  FileData* _data;
  size_t _read_pos;
  bool _open;
  bool _writing;

public:
  File() : _data(nullptr), _read_pos(0), _open(false), _writing(false) { }
  explicit File(FileData* data, bool writing = false)
    : _data(data), _read_pos(0), _open(data != nullptr), _writing(writing) { }

  // The driver signals "could not open" through the bool conversion, not an
  // exception or a null return.
  operator bool() const { return _open; }

  // Only a file that was opened for writing commits on close. A read never
  // changes what is on the medium.
  void close() {
    if (_open && _writing && _data != nullptr) {
      if (_data->bytes.size() > _data->commit_budget) {
        _data->bytes.resize(_data->commit_budget);
      }
      if (_data->corrupt_on_commit < _data->bytes.size()) {
        _data->bytes[_data->corrupt_on_commit] ^= 0xFF;
      }
    }
    _open = false;
  }

  // Overriding the single byte write would otherwise hide the buffer overloads
  // of Print, which is what the callers use.
  using Print::write;

  size_t write(uint8_t b) override {
    if (!_open || _data == nullptr || _data->write_budget == 0) return 0;
    _data->bytes.push_back(b);
    _data->write_budget--;
    return 1;
  }

  int available() override {
    if (!_open || _data == nullptr) return 0;
    return (int) (_data->bytes.size() - _read_pos);
  }

  int read() override {
    if (available() <= 0) return -1;
    return _data->bytes[_read_pos++];
  }

  int peek() override {
    if (available() <= 0) return -1;
    return _data->bytes[_read_pos];
  }

  // The driver's File offers a buffer read next to the Stream interface, and
  // IdentityStore uses it to pick up the display name.
  // After a sequential write both are the number of bytes that went in; the
  // callers of AtomicFile compare the size after close() with the position
  // before it.
  uint32_t position() { return _data ? (uint32_t) _data->bytes.size() : 0; }
  uint32_t size() { return _data ? (uint32_t) _data->bytes.size() : 0; }

  size_t read(uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (n < size && available() > 0) buffer[n++] = (uint8_t) read();
    return n;
  }
};

}  // namespace Adafruit_LittleFS_Namespace

class Adafruit_LittleFS {
  std::map<std::string, Adafruit_LittleFS_Namespace::FileData> _files;

public:
  // Test knobs.
  size_t write_budget = std::numeric_limits<size_t>::max();
  size_t commit_budget = std::numeric_limits<size_t>::max();
  size_t corrupt_on_commit = std::numeric_limits<size_t>::max();
  bool fail_open = false;

  bool exists(const char* path) { return _files.count(path) > 0; }
  bool remove(const char* path) { return _files.erase(path) > 0; }
  bool mkdir(const char* path) { return true; }

  // littlefs replaces an existing target in the same commit.
  bool fail_rename = false;
  bool rename(const char* from, const char* to) {
    if (fail_rename) return false;
    auto found = _files.find(from);
    if (found == _files.end()) return false;
    Adafruit_LittleFS_Namespace::FileData moved = found->second;
    _files.erase(found);
    _files[to] = moved;
    return true;
  }

  Adafruit_LittleFS_Namespace::File open(const char* path, uint8_t mode = FILE_O_READ) {
    if (fail_open) return Adafruit_LittleFS_Namespace::File();
    if (mode == FILE_O_WRITE) {
      // std::map keeps its values at a stable address, so handing out a
      // pointer stays valid while other files are added.
      Adafruit_LittleFS_Namespace::FileData& data = _files[path];
      data.bytes.clear();
      data.write_budget = write_budget;
      data.commit_budget = commit_budget;
      data.corrupt_on_commit = corrupt_on_commit;
      return Adafruit_LittleFS_Namespace::File(&data, true);
    }
    auto found = _files.find(path);
    if (found == _files.end()) return Adafruit_LittleFS_Namespace::File();
    return Adafruit_LittleFS_Namespace::File(&found->second);
  }

  // Lets a test see what actually reached the medium.
  const std::vector<uint8_t>* contents(const char* path) {
    auto found = _files.find(path);
    return found == _files.end() ? nullptr : &found->second.bytes;
  }
};
