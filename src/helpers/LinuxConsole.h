#pragma once

#if defined(ARDULINUX_PLATFORM) || defined(LINUX_PLATFORM)

#include <Arduino.h>
#include <stdio.h>
#include "PtyConsole.h"

// Arduino Stream adapter over PtyConsole.
//
// The repeater's text CLI reads from / writes to this instead of Serial, so the
// PTY carries only the CLI while Serial (stdout) keeps the debug logs — the
// clean log/CLI split. write() also mirrors each byte to stdout, so admin
// commands and their replies are still recorded in the log (journald) alongside
// the debug output, while the console stays free of log noise.
//
// A client attaches to the published PTY symlink, e.g.:
//   meshcore-cli -r -s /run/meshcored/console
//
// All the PTY mechanics live in PtyConsole (host-unit-tested); this adapter is a
// thin delegating shim.
class LinuxConsole : public Stream {
    PtyConsole _pty;

public:
    // Open the PTY and publish the symlink (see PtyConsole::begin). Returns true
    // on success; on failure the caller should fall back to Serial.
    bool begin(const char *link) { return _pty.begin(link); }
    void end() { _pty.end(); }
    bool isOpen() const { return _pty.isOpen(); }
    const char *path() const { return _pty.path(); }

    int available() override { return _pty.available(); }
    int read() override { return _pty.read(); }
    int peek() override { return _pty.peek(); }
    size_t write(uint8_t c) override {
        putchar(c);       // mirror to stdout so commands/replies reach journald
        _pty.write(c);    // and to the attached console client
        return 1;
    }
    void flush() override { fflush(stdout); _pty.flush(); }
    using Print::write;  // pull in write(str) / write(buf, size)
};

#endif // ARDULINUX_PLATFORM || LINUX_PLATFORM
