/* xnd-ulock.h */
#ifndef XND_ULOCK_H
#define XND_ULOCK_H

#include "common/atomic.h"
#include "common/compiler.h"
#include "xnd/xnd.h"
#include "ulock-defs.h"

struct xnd_ulock
{
  uint32_t ul_val;
};

#define XND_ULOCK_INITIALIZER { 0 }

#define XND_ULOCK_WAIT_OP (UL_UNFAIR_LOCK | ULF_NO_ERRNO)
#define XND_ULOCK_WAKE_OP (UL_UNFAIR_LOCK | ULF_NO_ERRNO)

#define xnd_ulock_init(ul) ((ul)->ul_val = 0, 0)

#define xnd_ulock_trylock(ul) \
  __xnd_likely (atomic_cmpxchg_acquire (&(ul)->ul_val, 0, 1))

extern void xnd_ulock_wait (struct xnd_ulock *);
extern void xnd_ulock_wake (struct xnd_ulock *);

#define xnd_ulock_lock(ul)                                                  \
  ((void) ({                                                                \
    struct xnd_ulock *__ul = (ul);                                          \
    if (__xnd_unlikely (!xnd_atomic_cmpxchg_acquire (&__ul->ul_val, 0, 1))) \
      xnd_ulock_wait (__ul);                                                \
  }))

#define xnd_ulock_unlock(ul)                                              \
  ((void) ({                                                              \
    struct xnd_ulock *__ul = (ul);                                        \
    if (__xnd_unlikely (xnd_atomic_xchg_release (&__ul->ul_val, 0) == 2)) \
      xnd_ulock_wake (__ul);                                              \
  }))

#endif /* XND_ULOCK_H */
