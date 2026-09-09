#pragma once

// MESHCORE_HOST_TEST enables this engine in the host unit-test env without
// claiming a real platform macro (LINUX_PLATFORM is used by the native-shim
// port).
#if defined(ARDULINUX_PLATFORM) || defined(LINUX_PLATFORM) || defined(MESHCORE_HOST_TEST)

#include <stddef.h>
#include <stdint.h>
#include <string>

// A local console carried over a pseudo-terminal (PTY), for the Linux repeater's
// text CLI.
//
// meshcored opens a PTY master and publishes a stable symlink to the slave
// device (e.g. /run/meshcored/console -> /dev/pts/N) so a serial client can
// attach to it directly:  meshcore-cli -r -s /run/meshcored/console
// (meshcore-cli's repeater mode drives a raw-text serial CLI via pyserial, which
// needs a tty — hence a PTY rather than a socket).
//
// Pure POSIX (no Arduino dependency) so the accept-free read/write/newline logic
// is unit-testable on the host; LinuxConsole wraps it in an Arduino Stream.
//
// The PTY master persists for the daemon's life; a client just opens/closes the
// slave, so there is no accept/reap. The slave device is chmod'd 0600 (the
// unauthenticated local CLI's access gate).
class PtyConsole {
    int master_fd = -1;      // PTY master; -1 when closed
    int peeked = -1;         // one-byte pushback for peek(); -1 when empty
    std::string link_path;   // published symlink to the slave (unlinked on end())
    std::string pts_path;    // the slave device path (/dev/pts/N)

public:
    PtyConsole() = default;
    ~PtyConsole();

    // Open the PTY and publish a symlink at `link` (empty => a per-user default:
    // $XDG_RUNTIME_DIR/meshcore/console, else /tmp/meshcore-<uid>/console).
    // Returns true on success; on failure logs to stderr and returns false so
    // the caller can fall back to the stdout console.
    bool begin(const char* link);
    void end();

    int available();         // bytes available from the client (0 if none)
    int peek();              // peek one byte without consuming (-1 if none)
    int read();              // read one byte (-1 if none); maps '\n' -> '\r'
    size_t write(uint8_t c); // write one byte to the client; -> 1
    void flush() {}

    bool isOpen() const { return master_fd != -1; }
    // Path a client should open: the published symlink, else the raw pts device.
    const char* path() const {
        return link_path.empty() ? pts_path.c_str() : link_path.c_str();
    }
};

#endif // ARDULINUX_PLATFORM || LINUX_PLATFORM || MESHCORE_HOST_TEST
