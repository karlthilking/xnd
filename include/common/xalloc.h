/* xalloc.h */
#ifndef XND_XALLOC_H
#define XND_XALLOC_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"

#define xalloc_panic(msg, errnum)			\
  do {							\
    if (isatty (2))					\
      dprintf (2, msg ": %s\n", strerror (errnum));	\
    exit (1);						\
  } while (0)

#define __xalloc(op, ...)			\
  ({						\
    void *__p = op (__VA_ARGS__);		\
    if (unlikely (__p == NULL))			\
      xalloc_panic (TOSTRING (op), errno);	\
    __p;					\
  })

#define xmalloc(n)				\
  ({						\
    size_t __n = (n);				\
    __xalloc (malloc, __n);			\
  })

#define xcalloc(cnt, size)			\
  ({						\
    size_t __cnt = (cnt), __size = (size);	\
    __xalloc (calloc, __cnt, __size);		\
  })

#define xrealloc(ptr, size)			\
  ({						\
    void *__ptr = (ptr);			\
    size_t __size = (size);			\
    __xalloc (realloc, __ptr, __size);		\
  })

#define xaligned_alloc(align, size)	       \
  ({					       \
    size_t __align = (align), __size = (size); \
    __xalloc (aligned_alloc, __align, __size); \
  })

#define xposix_memalign(ptr, align, size)			\
  ((void)							\
   ({								\
     void **__ptr = (ptr);					\
     size_t __align = (align), __size = (size);			\
     int __err = posix_memalign (__ptr, __align, __size);	\
     if (unlikely (__err != 0))					\
       xalloc_panic ("posix_memalign", __err);			\
  }))

#endif /* XND_XALLOC_H */
