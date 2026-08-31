// Unit tests for PtyConsole (src/helpers/PtyConsole.cpp).
//
// Wired into [env:native] via -D MESHCORE_HOST_TEST and the PtyConsole.cpp entry
// in build_src_filter; both are required, since the engine is compiled out
// without the macro and the suite would not link without the source.
//
// Run: pio test -e native -f test_console

#include <gtest/gtest.h>

#include "helpers/PtyConsole.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string>

namespace {

std::string unique_link(const char *tag) {
    return std::string("/tmp/meshcore-test-pty-") + tag + "-" +
           std::to_string(getpid());
}

// Open the PTY slave (via the console's published path) as a raw serial client.
int open_client(const char *path) {
    int fd = ::open(path, O_RDWR | O_NOCTTY);
    EXPECT_GE(fd, 0);
    if (fd >= 0) {
        termios t{};
        if (tcgetattr(fd, &t) == 0) { cfmakeraw(&t); tcsetattr(fd, TCSANOW, &t); }
    }
    return fd;
}

int wait_available(PtyConsole &c, int want) {
    int avail = 0;
    for (int i = 0; i < 200 && avail < want; i++) {
        avail = c.available();
        if (avail < want) usleep(1000);
    }
    return avail;
}

// Read like the repeater's loop(): only read() while available() reports bytes.
std::string read_like_consumer(PtyConsole &c, char terminator) {
    std::string out;
    for (int i = 0; i < 400 && out.find(terminator) == std::string::npos; i++) {
        while (c.available() > 0) {
            int ch = c.read();
            if (ch < 0) break;
            out += (char)ch;
        }
        if (out.find(terminator) == std::string::npos) usleep(1000);
    }
    return out;
}

}  // namespace

TEST(PtyConsole, DeliversClientBytesToRead) {
    PtyConsole c;
    ASSERT_TRUE(c.begin(unique_link("read").c_str()));
    int client = open_client(c.path());
    ASSERT_GE(client, 0);
    ASSERT_EQ(::write(client, "hi", 2), 2);

    EXPECT_GE(wait_available(c, 2), 2);
    EXPECT_EQ(c.read(), 'h');
    EXPECT_EQ(c.read(), 'i');

    ::close(client);
    c.end();
}

TEST(PtyConsole, MapsNewlineToCarriageReturn) {
    // The CLI terminates a command on '\r'; tools send '\n'. Map 1:1 so it works.
    PtyConsole c;
    ASSERT_TRUE(c.begin(unique_link("nl").c_str()));
    int client = open_client(c.path());
    ASSERT_GE(client, 0);
    ASSERT_EQ(::write(client, "hi\n", 3), 3);
    EXPECT_GE(wait_available(c, 3), 3);

    EXPECT_EQ(c.read(), 'h');
    EXPECT_EQ(c.read(), 'i');
    EXPECT_EQ(c.read(), '\r');

    ::close(client);
    c.end();
}

TEST(PtyConsole, WriteReachesConnectedClient) {
    PtyConsole c;
    ASSERT_TRUE(c.begin(unique_link("write").c_str()));
    int client = open_client(c.path());
    ASSERT_GE(client, 0);

    c.write((uint8_t)'O');
    c.write((uint8_t)'K');

    char buf[2] = {0, 0};
    ssize_t got = 0;
    for (int i = 0; i < 200 && got < 2; i++) {
        ssize_t n = ::read(client, buf + got, 2 - got);
        if (n > 0) got += n; else usleep(1000);
    }
    EXPECT_EQ(got, 2);
    EXPECT_EQ(buf[0], 'O');
    EXPECT_EQ(buf[1], 'K');

    ::close(client);
    c.end();
}

TEST(PtyConsole, PeekDoesNotConsume) {
    PtyConsole c;
    ASSERT_TRUE(c.begin(unique_link("peek").c_str()));
    int client = open_client(c.path());
    ASSERT_GE(client, 0);
    ASSERT_EQ(::write(client, "Z", 1), 1);
    EXPECT_GE(wait_available(c, 1), 1);

    EXPECT_EQ(c.peek(), 'Z');
    EXPECT_EQ(c.peek(), 'Z');
    EXPECT_EQ(c.read(), 'Z');
    EXPECT_EQ(c.read(), -1);

    ::close(client);
    c.end();
}

TEST(PtyConsole, NoInputBeforeClientWrites) {
    PtyConsole c;
    ASSERT_TRUE(c.begin(unique_link("noinput").c_str()));

    EXPECT_EQ(c.available(), 0);
    EXPECT_EQ(c.read(), -1);
    EXPECT_EQ(c.peek(), -1);

    c.end();
}

TEST(PtyConsole, ServesNewClientAfterCloseConsumerLoopShape) {
    // The master persists across client open/close; the consumer only read()s
    // when available()>0.
    PtyConsole c;
    ASSERT_TRUE(c.begin(unique_link("reconnect").c_str()));

    int c1 = open_client(c.path());
    ASSERT_GE(c1, 0);
    ASSERT_EQ(::write(c1, "a\n", 2), 2);
    EXPECT_EQ(read_like_consumer(c, '\r'), "a\r");
    ::close(c1);

    for (int i = 0; i < 20; i++) { c.available(); usleep(1000); }

    int c2 = open_client(c.path());
    ASSERT_GE(c2, 0);
    ASSERT_EQ(::write(c2, "b\n", 2), 2);
    EXPECT_EQ(read_like_consumer(c, '\r'), "b\r");

    ::close(c2);
    c.end();
}

TEST(PtyConsole, WriteAfterClientCloseDoesNotCrash) {
    PtyConsole c;
    ASSERT_TRUE(c.begin(unique_link("wclose").c_str()));
    int client = open_client(c.path());
    ASSERT_GE(client, 0);
    c.write((uint8_t)'x');
    ::close(client);

    usleep(20000);
    for (int i = 0; i < 200; i++) c.write((uint8_t)'y');
    SUCCEED();  // no signal / crash

    c.end();
}

TEST(PtyConsole, SlaveIsOwnerOnlyCharDevAndSymlinkUnlinkedOnEnd) {
    std::string link = unique_link("perms");
    PtyConsole c;
    ASSERT_TRUE(c.begin(link.c_str()));

    struct stat st{};
    ASSERT_EQ(::stat(c.path(), &st), 0);   // follows the symlink to the pts device
    EXPECT_TRUE(S_ISCHR(st.st_mode));
    EXPECT_EQ(st.st_mode & 0777, 0600u);   // owner-only: the access gate

    c.end();
    struct stat ls{};
    EXPECT_NE(::lstat(link.c_str(), &ls), 0);  // published symlink removed
}

// --- lifecycle, default link resolution, and degraded paths --------------------

TEST(PtyConsole, BeginIsIdempotent) {
    // A second begin() on an open console is a no-op: the original PTY and
    // published link stay put, and the new link argument is ignored.
    PtyConsole c;
    std::string link = unique_link("idem");
    std::string ignored = unique_link("idem-ignored");
    ASSERT_TRUE(c.begin(link.c_str()));
    std::string first = c.path();

    EXPECT_TRUE(c.begin(ignored.c_str()));
    EXPECT_EQ(std::string(c.path()), first);
    struct stat ls{};
    EXPECT_NE(::lstat(ignored.c_str(), &ls), 0);  // second link never published

    c.end();
}

TEST(PtyConsole, DefaultLinkUsesXdgRuntimeDir) {
    char dir[] = "/tmp/meshcore-test-xdg-XXXXXX";
    ASSERT_NE(mkdtemp(dir), nullptr);
    const char *prev = getenv("XDG_RUNTIME_DIR");
    const bool had = prev != nullptr;
    const std::string saved = had ? prev : "";
    setenv("XDG_RUNTIME_DIR", dir, 1);

    {
        PtyConsole c;
        ASSERT_TRUE(c.begin(nullptr));  // null link => default resolution
        EXPECT_EQ(std::string(c.path()), std::string(dir) + "/meshcore/console");
        struct stat st{};
        EXPECT_EQ(::stat(c.path(), &st), 0);
        EXPECT_TRUE(S_ISCHR(st.st_mode));
        c.end();
    }

    rmdir((std::string(dir) + "/meshcore").c_str());
    rmdir(dir);
    if (had) setenv("XDG_RUNTIME_DIR", saved.c_str(), 1); else unsetenv("XDG_RUNTIME_DIR");
}

TEST(PtyConsole, DefaultLinkFallsBackToTmpWithoutXdgRuntimeDir) {
    const char *prev = getenv("XDG_RUNTIME_DIR");
    const bool had = prev != nullptr;
    const std::string saved = had ? prev : "";
    unsetenv("XDG_RUNTIME_DIR");

    {
        PtyConsole c;
        ASSERT_TRUE(c.begin(""));  // empty link => default resolution
        EXPECT_EQ(std::string(c.path()),
                  "/tmp/meshcore-" + std::to_string((unsigned)getuid()) + "/console");
        c.end();
    }

    if (had) setenv("XDG_RUNTIME_DIR", saved.c_str(), 1);
}

TEST(PtyConsole, PathFallsBackToPtsDeviceWhenSymlinkFails) {
    // An unpublishable link is not fatal: begin() still succeeds and path()
    // hands the client the raw pts device instead.
    PtyConsole c;
    ASSERT_TRUE(c.begin("/nonexistent-meshcore-dir/console"));
    EXPECT_EQ(std::string(c.path()).rfind("/dev/pts/", 0), 0u);
    struct stat st{};
    EXPECT_EQ(::stat(c.path(), &st), 0);
    EXPECT_TRUE(S_ISCHR(st.st_mode));

    c.end();  // no link_path to unlink
    EXPECT_FALSE(c.isOpen());
}

TEST(PtyConsole, AvailableCountsPeekedByteAlongsideQueue) {
    PtyConsole c;
    ASSERT_TRUE(c.begin(unique_link("avail").c_str()));
    int client = open_client(c.path());
    ASSERT_GE(client, 0);
    ASSERT_EQ(::write(client, "ab", 2), 2);
    EXPECT_GE(wait_available(c, 2), 2);

    EXPECT_EQ(c.peek(), 'a');     // pulls one byte into the pushback slot
    EXPECT_EQ(c.available(), 2);  // 1 pushed back + 1 still queued in the PTY
    EXPECT_EQ(c.read(), 'a');
    EXPECT_EQ(c.read(), 'b');

    ::close(client);
    c.end();
}

TEST(PtyConsole, IsOpenTracksLifecycleAndEndIsIdempotent) {
    PtyConsole c;
    EXPECT_FALSE(c.isOpen());
    ASSERT_TRUE(c.begin(unique_link("life").c_str()));
    EXPECT_TRUE(c.isOpen());
    c.flush();  // no-op; writes are unbuffered

    c.end();
    EXPECT_FALSE(c.isOpen());
    c.end();  // idempotent: both guards already false

    // Every accessor stays inert rather than faulting once closed.
    EXPECT_EQ(c.available(), 0);
    EXPECT_EQ(c.read(), -1);
    EXPECT_EQ(c.peek(), -1);
    EXPECT_EQ(c.write((uint8_t)'x'), 1u);  // reports the byte consumed, drops it
    c.flush();
}

TEST(PtyConsole, DestructorUnpublishesSymlink) {
    std::string link = unique_link("dtor");
    {
        PtyConsole c;
        ASSERT_TRUE(c.begin(link.c_str()));
        struct stat ls{};
        ASSERT_EQ(::lstat(link.c_str(), &ls), 0);
    }  // ~PtyConsole() -> end()

    struct stat ls{};
    EXPECT_NE(::lstat(link.c_str(), &ls), 0);
}

TEST(PtyConsole, BeginFailsCleanlyWhenNoDescriptorIsAvailable) {
    // Exhausting the fd table makes posix_openpt() fail. begin() must report the
    // failure rather than half-open, so the caller can fall back to Serial.
    rlimit orig{};
    ASSERT_EQ(getrlimit(RLIMIT_NOFILE, &orig), 0);
    rlimit tight = orig;
    tight.rlim_cur = 3;  // stdin/stdout/stderr hold 0..2; nothing left to hand out
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &tight), 0);

    bool opened = true;
    {
        PtyConsole c;
        opened = c.begin(unique_link("nofd").c_str());
        EXPECT_FALSE(c.isOpen());
        c.end();  // safe on a console that never opened
    }
    ASSERT_EQ(setrlimit(RLIMIT_NOFILE, &orig), 0);
    EXPECT_FALSE(opened);
}

// --- syscall interposition ----------------------------------------------------
//
// grantpt() and ptsname_r() cannot fail once posix_openpt() has handed back a
// valid master, so their error paths are unreachable from the outside. These
// strong definitions win over libc's at link time, letting a test force each
// failure; when the flag is clear they forward to the real implementation.

namespace {
bool fail_grantpt = false;
bool fail_ptsname_r = false;
}  // namespace

extern "C" int grantpt(int fd) {
    using fn_t = int (*)(int);
    static fn_t real = reinterpret_cast<fn_t>(dlsym(RTLD_NEXT, "grantpt"));
    if (fail_grantpt) { errno = EACCES; return -1; }
    return real(fd);
}

extern "C" int ptsname_r(int fd, char *buf, size_t buflen) {
    using fn_t = int (*)(int, char *, size_t);
    static fn_t real = reinterpret_cast<fn_t>(dlsym(RTLD_NEXT, "ptsname_r"));
    if (fail_ptsname_r) { errno = ERANGE; return -1; }
    return real(fd, buf, buflen);
}

TEST(PtyConsole, BeginFailsCleanlyWhenGrantptFails) {
    fail_grantpt = true;
    {
        PtyConsole c;
        EXPECT_FALSE(c.begin(unique_link("grantpt").c_str()));
        EXPECT_FALSE(c.isOpen());
    }
    fail_grantpt = false;
}

TEST(PtyConsole, BeginFailsCleanlyWhenSlaveNameCannotBeResolved) {
    fail_ptsname_r = true;
    {
        PtyConsole c;
        EXPECT_FALSE(c.begin(unique_link("ptsname").c_str()));
        EXPECT_FALSE(c.isOpen());
    }
    fail_ptsname_r = false;
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
