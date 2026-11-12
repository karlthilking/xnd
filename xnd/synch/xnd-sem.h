/* xnd-sem.h */
#ifndef XND_SEM_H
#define XND_SEM_H

#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "common/atomic.h"
#include "ulock-defs.h"

struct xnd_sem
{
  uint32_t val;
};

#define XND_SEM_WAIT_OP (UL_COMPARE_AND_WAIT | ULF_NO_ERRNO)
#define XND_SEM_WAKE_OP (UL_COMPARE_AND_WAIT | ULF_NO_ERRNO)

#define xnd_sem_init(sem, v) ((sem)->val = (v), 0)

#define XND_SEM_WAITERS     0x80000000U
#define XND_SEM_VALUE_MASK  0x7FFFFFFFU

__BEGIN_DECLS

/* Returns EINVAL for an invalid nanosecond timeout value, and
   ETIMEDOUT if the timeout expired. A timeout of 0 indicates no
   timeout; xnd_sem_trywait can be used to attempt to decrement the
   semaphore value without blocking. */
extern int xnd_sem_wait_slow (struct xnd_sem *, uint64_t);
extern void xnd_sem_post_slow (struct xnd_sem *);

static inline int
xnd_sem_trywait (struct xnd_sem *sem)
{
  uint32_t val;

  val = xnd_atomic_load_relaxed (&sem->val);
  if ((val & XND_SEM_VALUE_MASK) > 0)
    if (xnd_atomic_cmpxchg_acquire (&sem->val, val, val - 1))
      return 0;

  return EBUSY;
}

static inline void
xnd_sem_wait (struct xnd_sem *sem)
{
  if (xnd_sem_trywait (sem) != 0)
    xnd_sem_wait_slow (sem, 0 /* unused timeout */);
}

static inline int
xnd_sem_timedwait (struct xnd_sem *sem, uint64_t timeout)
{
  if (xnd_sem_trywait (sem) != 0)
    return xnd_sem_wait_slow (sem, timeout);

  return 0;
}

static inline void
xnd_sem_post (struct xnd_sem *sem)
{
  uint32_t expected, val;

  val = xnd_atomic_load_relaxed (&sem->val);
  if ((val & XND_SEM_WAITERS) == 0)
    {
      expected = val;
      val = (val & XND_SEM_VALUE_MASK) + 1;
      if (xnd_atomic_cmpxchg_release (&sem->val, expected, val))
        return;
    }

  xnd_sem_post_slow (sem);
}

__END_DECLS

#endif /* XND_SEM_H */
