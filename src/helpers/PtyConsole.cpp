#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // ptsname_r()
#endif

#include "PtyConsole.h"

#if defined(ARDULINUX_PLATFORM) || defined(LINUX_PLATFORM) || defined(MESHCORE_HOST_TEST)

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace {

// Resolve the symlink path to publish for the PTY slave. Non-empty `link` is
// used verbatim; otherwise $XDG_RUNTIME_DIR/meshcore/console, else
// /tmp/meshcore-<uid>/console, with the parent directory created mode 0700.
std::string resolveLinkPath(const char *link) {
    if (link && *link) return std::string(link);

    const char *xdg = getenv("XDG_RUNTIME_DIR");
    std::string dir = (xdg && *xdg)
        ? std::string(xdg) + "/meshcore"
        : std::string("/tmp/meshcore-") + std::to_string((unsigned)getuid());
    mkdir(dir.c_str(), 0700);  // best effort
    return dir + "/console";
}

}  // namespace

PtyConsole::~PtyConsole() { end(); }

bool PtyConsole::begin(const char *link) {
    if (master_fd != -1) return true;  // already open

    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "meshcore: console posix_openpt() failed: %s\n", strerror(errno));
        return false;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    if (grantpt(fd) != 0 || unlockpt(fd) != 0) {
        fprintf(stderr, "meshcore: console grantpt/unlockpt failed: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    char buf[128];
    if (ptsname_r(fd, buf, sizeof(buf)) != 0) {
        fprintf(stderr, "meshcore: console ptsname_r failed: %s\n", strerror(errno));
        close(fd);
        return false;
    }
    pts_path = buf;

    // Raw line discipline: no echo / canonical / CR-NL translation, so bytes
    // pass through unchanged (our read() does the '\n'->'\r' mapping itself).
    struct termios t;
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(fd, TCSANOW, &t);
    }

    // Owner-only: attaching to the console grants the privileged local CLI.
    chmod(pts_path.c_str(), 0600);

    // Publish a stable symlink so clients have a fixed path across restarts
    // (the /dev/pts/N number varies). If the symlink can't be made, clients can
    // still use the raw pts path (path() falls back to it).
    std::string lp = resolveLinkPath(link);
    unlink(lp.c_str());
    if (symlink(pts_path.c_str(), lp.c_str()) == 0) {
        link_path = lp;
    } else {
        fprintf(stderr, "meshcore: console symlink(%s) failed: %s; use %s\n",
                lp.c_str(), strerror(errno), pts_path.c_str());
    }

    master_fd = fd;
    return true;
}

void PtyConsole::end() {
    if (!link_path.empty()) { unlink(link_path.c_str()); link_path.clear(); }
    if (master_fd != -1) { close(master_fd); master_fd = -1; }
    pts_path.clear();
    peeked = -1;
}

int PtyConsole::available() {
    int n = (peeked >= 0) ? 1 : 0;
    if (master_fd != -1) {
        int q = 0;
        if (ioctl(master_fd, FIONREAD, &q) == 0 && q > 0) n += q;
    }
    return n;
}

int PtyConsole::peek() {
    if (peeked < 0) peeked = read();
    return peeked;
}

int PtyConsole::read() {
    if (peeked >= 0) { int c = peeked; peeked = -1; return c; }
    if (master_fd == -1) return -1;
    unsigned char b;
    ssize_t n = ::read(master_fd, &b, 1);
    // Map '\n' -> '\r' (1:1) so line-oriented CLIs that terminate on '\r' work
    // with tools that send '\n'. Kept 1:1 so available()/read() stay consistent
    // (the repeater's read() is unchecked).
    if (n == 1) return (b == '\n') ? '\r' : b;
    // n == 0 (no slave open) or n < 0 (EAGAIN, or EIO after the client closed):
    // no data right now. The master persists; a client can reattach.
    return -1;
}

size_t PtyConsole::write(uint8_t c) {
    if (master_fd != -1) {
        // A PTY master write with no reader just buffers (or EAGAIN/EIO under
        // O_NONBLOCK) — no SIGPIPE — so unwritten console output is simply
        // dropped, never fatal.
        ssize_t r = ::write(master_fd, &c, 1);
        (void)r;
    }
    return 1;
}

#endif // ARDULINUX_PLATFORM || LINUX_PLATFORM || MESHCORE_HOST_TEST
