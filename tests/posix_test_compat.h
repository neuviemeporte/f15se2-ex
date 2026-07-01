#pragma once

#if defined(_WIN32)

#include <cstddef>
#include <cstdlib>
#include <io.h>
#include <windows.h>

using pid_t = int;

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED ((void *)-1)
#define _SC_PAGESIZE 1

static inline pid_t fork(void) { return -1; }
static inline pid_t waitpid(pid_t, int *, int) { return -1; }
static inline int WIFEXITED(int) { return 0; }
static inline int WEXITSTATUS(int) { return 0; }
static inline int WIFSIGNALED(int) { return 0; }
static inline int WTERMSIG(int) { return 0; }
static inline int mprotect(void *, std::size_t, int) { return -1; }
static inline void *mmap(void *, std::size_t, int, int, int, int) { return MAP_FAILED; }
static inline int munmap(void *, std::size_t) { return -1; }
static inline long sysconf(int) { return 4096; }
static inline int dup(int fd) { return _dup(fd); }
static inline int dup2(int fd1, int fd2) { return _dup2(fd1, fd2); }
static inline int close(int fd) { return _close(fd); }
static inline int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && std::getenv(name) != nullptr) return 0;
    return _putenv_s(name, value);
}
static inline void usleep(unsigned int usec) {
    Sleep((usec + 999u) / 1000u);
}

#else

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#endif
