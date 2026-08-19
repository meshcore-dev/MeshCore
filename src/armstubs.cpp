// https://github.com/meshcore-dev/MeshCore/issues/2469
// armstubs.cpp exists to silence warnings introduced after upgrading platformio/toolchain-gccarmnoneeabi
extern "C" __attribute__((weak)) int _close(int fd)  { return -1; }
extern "C" __attribute__((weak)) int _lseek(int fd, int offset, int whence) { return 0; }
extern "C" __attribute__((weak)) int _read(int fd, char *buf, int len)  { return 0; }
extern "C" __attribute__((weak)) int _fstat(int fd, struct stat *st) { return 0; }
extern "C" __attribute__((weak)) int _isatty(int fd) { return 1; }
extern "C" __attribute__((weak)) int _getpid(void) { return 1; }
extern "C" __attribute__((weak)) int _kill(int pid, int sig) { return -1; }
