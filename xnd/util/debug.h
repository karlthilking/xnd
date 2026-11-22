/* debug.h */
#ifndef XND_DEBUG_H
#define XND_DEBUG_H

#include "xnd/xnd.h"

#define XND_DEBUG_PAUSE_WHILE(expr)                       \
  do {                                                    \
    if (expr)                                             \
      {                                                   \
        const char __fmt[] = "Pausing in %s (pid: %d)\n"; \
        printf (__fmt, __func__, xnd_sys_getpid ());      \
        do { /* nothing */ } while (expr);                \
      }                                                   \
  } while (0)

#define XND_DEBUG_PAUSE()                       \
  do {                                          \
    bool debug_pause = true;                    \
    XND_DEBUG_PAUSE_WHILE (debug_pause);        \
  } while (0)

void dump_debug_info (void);

#endif
