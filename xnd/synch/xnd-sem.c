/* xnd-sem.c */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "xnd-sem.h"
#include "ulock-defs.h"
#include "common/atomic.h"
#include "common/time.h"

static void xnd_sem_panic (const char *, int) __cold __noreturn;
static inline int xnd_sem_wait_timeout (uint64_t, uint64_t *)
  __nonnull__ ((2));
static inline int xnd_sem_wait_deadline (uint64_t, uint64_t *)
  __nonnull__ ((2));

static void
xnd_sem_panic (const char *msg, int errnum)
{
  char buf[128];

  buf[sizeof (buf) - 1] = '\0';
  strerror_r (errnum, buf, sizeof (buf) - 1);
  dprintf (STDERR_FILENO, "%s: %s (%s)\n", msg, buf, __FILE_NAME__);

  abort ();
}

static inline int
xnd_sem_wait_timeout (uint64_t deadline, uint64_t *timeout)
{
  uint64_t now;

  now = clock_gettime_nsec_np (CLOCK_UPTIME_RAW);
  if (now >= deadline)
    return ETIMEDOUT;

  *timeout = deadline - now;
  return 0;
}

/* Deadline is specified in nanoseconds for __ulock_wait2 */
static inline int
xnd_sem_wait_deadline (uint64_t timeout, uint64_t *deadline)
{
  uint64_t now;

  now = clock_gettime_nsec_np (CLOCK_UPTIME_RAW);
  if (xnd_add_overflow (now, timeout, deadline))
    return EINVAL;

  return 0;
}

int
xnd_sem_wait_slow (struct xnd_sem *sem, uint64_t timeout)
{
  int err;
  uint32_t expected, val;
  uint64_t deadline = 0;

  if (timeout != 0)
    {
      err = xnd_sem_wait_deadline (timeout, &deadline);
      if (err != 0)
        return err;
    }
  
  for (;;)
    {
      val = xnd_atomic_load_relaxed (&sem->val);
      if ((val & XND_SEM_VALUE_MASK) > 0
          && xnd_atomic_cmpxchg_weak_acquire (&sem->val, val, val - 1))
        return 0;

      if ((val & XND_SEM_WAITERS) == 0)
        {
          expected = val;
          val |= XND_SEM_WAITERS;
          if (!xnd_atomic_cmpxchg_weak_acquire (&sem->val, expected, val))
            continue;
        }

      if (deadline != 0)
        {
          err = xnd_sem_wait_timeout (deadline, &timeout);
          if (err != 0)
            return err;
        }

      err = __ulock_wait2 (XND_SEM_WAIT_OP, &sem->val, val, timeout, 0);
      if (__xnd_unlikely (err < 0))
        {
          if (err == -ETIMEDOUT)
            return ETIMEDOUT;
          else if (err != -EINTR && err != -EFAULT)
            {
              xnd_sem_panic ("__ulock_wait2", -err);
              unreachable ();
            }
        }
    }

  return 0;
}

void
xnd_sem_post_slow (struct xnd_sem *sem)
{
  int err;

  xnd_atomic_inc_release (&sem->val);
  do
    {
      err = __ulock_wake (XND_SEM_WAKE_OP, &sem->val, 0);
      if (__xnd_unlikely (err < 0))
        {
          if (err != -EINTR && err != -ENOENT)
            {
              xnd_sem_panic ("__ulock_wake", -err);
              unreachable ();
            }
        }
    }
  while (err == -EINTR);
}
