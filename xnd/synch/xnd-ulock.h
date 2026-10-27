/* xnd-ulock.h */
#ifndef XND_ULOCK_H
#define XND_ULOCK_H

#include "common/atomic.h"
#include "common/compiler.h"
#include "xnd/xnd.h"
#include "xnd/tls.h"
#include "ulock-defs.h"

struct xnd_ulock
{
  uint64_t ul_val;
};
#define XND_ULOCK_INITIALIZER {0}

#define XND_ULOCK_WAITERS			0x1
#define XND_ULOCK_OWNER_MASK	~(XND_ULOCK_WAITERS)

#define XND_ULOCK_WAIT_OP (UL_COMPARE_AND_WAIT64 | ULF_NO_ERRNO)
#define XND_ULOCK_WAKE_OP (UL_COMPARE_AND_WAIT64 | ULF_NO_ERRNO)

#define xnd_ulock_self() 		((uint64_t) self_tsd_base ())
#define xnd_ulock_init(ul) 	((ul)->ul_val = 0, 0)

extern void xnd_ulock_wait (struct xnd_ulock *, uint64_t);
extern void xnd_ulock_wake (struct xnd_ulock *);

static inline int
xnd_ulock_trylock (struct xnd_ulock *ul)
{
  uint64_t self = xnd_ulock_self ();

  if (xnd_atomic_cmpxchg_acquire (&ul->ul_val, 0, self))
    return 0;

  return 1;
}

static inline void
xnd_ulock_lock (struct xnd_ulock *ul)
{
  if (__xnd_unlikely (xnd_ulock_trylock (ul) != 0))
    xnd_ulock_wait (ul, xnd_ulock_self ());
}

static inline void
xnd_ulock_unlock (struct xnd_ulock *ul)
{
  uint64_t val;

  val = xnd_atomic_xchg_release (&ul->ul_val, 0);
  if (__xnd_unlikely ((val & XND_ULOCK_WAITERS) != 0))
    xnd_ulock_wake (ul);
}

#define xnd_ulock_owner(ul) \
  (xnd_atomic_load_relaxed (&(ul)->ul_val) & XND_ULOCK_OWNER_MASK)

#define xnd_ulock_owned(ul) \
  (xnd_ulock_owner (ul) == xnd_ulock_self ())

#define xnd_ulock_assert_owner(ul) \
  xnd_assert (xnd_ulock_owned (ul))

#endif /* XND_ULOCK_H */
