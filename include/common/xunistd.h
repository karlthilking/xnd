/* xunistd.h */
#ifndef XND_XUNISTD_H
#define XND_XUNISTD_H

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#include "compiler.h"

#define __xop(op, failed, ...)                  \
  ({                                            \
    __auto_type __ret = op (__VA_ARGS__);       \
    if (__xnd_unlikely (__ret == failed))       \
      {                                         \
        perror (STRINGIFY (op));                \
        exit (1);                               \
      }                                         \
    __ret;                                      \
  })

#define xopen(path, oflag, ...) \
  __xop (open, -1, path, oflag, ##__VA_ARGS__)
#define xopenat(fd, path, oflag, ...) \
  __xop (openat, -1, fd, path, oflag, ##__VA_ARGS__)
#define xcreat(path, mode) \
  __xop (creat, -1, path, mode)
#define xfcntl(fd, cmd, ...) \
  __xop (fcntl, -1, fd, cmd, ##__VA_ARGS__)
#define xclose(fd) \
  __xop (close, -1, fd)

#define xdup(fd) \
  __xop (dup, -1, fd)
#define xdup2(ofd, nfd) \
  __xop (dup2, -1, ofd, nfd)

#define xunlink(path) \
  __xop (unlink, -1, path)

#define xstat(path, st) \
  __xop (stat, -1, path, st)
#define xfstat(fd, st) \
  __xop (fstat, -1, fd, st)

#define xlseek(fd, offset, whence) \
  __xop (lseek, -1, fd, offset, whence)

#define xmmap(addr, len, prot, flags, fd, offset) \
  __xop (mmap, MAP_FAILED, addr, len, prot, flags, fd, offset)
#define xmunmap(addr, len) \
  __xop (munmap, -1, addr, len)
#define xmprotect(addr, len, prot) \
  __xop (mprotect, -1, addr, len, prot)

#define xfork() \
  __xop (fork, -1)
#define xexecl(path, arg0, ...) \
  __xop (execl, -1, path, arg0, ##__VA_ARGS__)
#define xexeclp(file, arg0, ...) \
  __xop (execlp, -1, file, arg0, ##__VA_ARGS__)
#define xexecv(path, argv) \
  __xop (execv, -1, path, argv)
#define xexecvp(file, argv) \
  __xop (execvp, -1, file, argv)

#define xwait(status) \
  __xop (wait, status)
#define xwaitpid(pid, status, options) \
  __xop (waitpid, pid, status, options)

#endif /* XND_UNISTD_H */
