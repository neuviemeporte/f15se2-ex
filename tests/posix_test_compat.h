#pragma once

#if defined(_WIN32)

#include <cstddef>

using pid_t = int;

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED ((void *)-1)

static inline pid_t fork(void) { return -1; }
static inline pid_t waitpid(pid_t, int *, int) { return -1; }
static inline int WIFEXITED(int) { return 0; }
static inline int WEXITSTATUS(int) { return 0; }
static inline int WIFSIGNALED(int) { return 0; }
static inline int WTERMSIG(int) { return 0; }
static inline int mprotect(void *, std::size_t, int) { return -1; }
static inline void *mmap(void *, std::size_t, int, int, int, int) { return MAP_FAILED; }
static inline int munmap(void *, std::size_t) { return -1; }

#else

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#endif
