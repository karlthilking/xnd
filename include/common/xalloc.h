/* xalloc.h */
#ifndef XND_XALLOC_H
#define XND_XALLOC_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"

#define xalloc_panic(msg, errnum)                     \
  do                                                  \
    {                                                 \
      if (isatty (2))                                 \
        dprintf (2, msg ": %s\n", strerror (errnum)); \
      exit (1);                                       \
    }                                                 \
  while (0)

#define __xalloc(op, ...)                  \
  ({                                       \
    void *__p = op (__VA_ARGS__);          \
    if (__xnd_unlikely (__p == NULL))      \
      xalloc_panic (TOSTRING (op), errno); \
    __p;                                   \
  })

#define xmalloc(n) 		__xalloc (malloc, n)
#define xcalloc(c, n)		__xalloc (calloc, c, n)
#define xrealloc(p, n)		__xalloc (realloc, p, n)
#define xaligned_alloc(a, n)	__xalloc (aligned_alloc, a, n)

#define xstrdup(str) 		__xalloc (strdup, str)
#define xstrndup(str, n) 	__xalloc (strndup, str, n)

#define __xallocf(op, ptr, fmt, ...)            \
  ((void)                                       \
   ({                                           \
     int __ret = op (ptr, fmt, ##__VA_ARGS__);  \
     if (__xnd_unlikely (__ret == -1))          \
       xalloc_panic (STRINGIFY (op), errno);    \
   }))

#define xasprintf(ptr, fmt, ...) \
  __xallocf (asprintf, ptr, fmt, ##__VA_ARGS__)
#define xvasprintf(ptr, fmt, ap) \
  __xallocf (vasprintf, ptr, fmt, ap)

#define xposix_memalign(ptr, align, size)                       \
  ((void)                                                       \
   ({                                                           \
     void **__ptr = (ptr);                                      \
     size_t __align = (align), __size = (size);                 \
     int __err = posix_memalign (__ptr, __align, __size);       \
     if (__xnd_unlikely (__err != 0))                           \
       xalloc_panic ("posix_memalign", __err);                  \
   }))

#endif /* XND_XALLOC_H */
