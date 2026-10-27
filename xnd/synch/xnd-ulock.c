/* xnd-ulock.c */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/compiler.h"
#include "xnd-ulock.h"

static void xnd_ulock_panic (const char *, int) __cold __noreturn;

static void
xnd_ulock_panic (const char *msg, int errnum)
{
  char buf[128];

  if (isatty (STDERR_FILENO))
    {
      strerror_r (errnum, buf, sizeof (buf) - 1);
      buf[sizeof (buf) - 1] = '\0';
      dprintf (STDERR_FILENO, "%s: %s: %s\n", __FILE_NAME__, msg, buf);
    }

  exit (1);
}

void
xnd_ulock_wait (struct xnd_ulock *ul, uint64_t self)
{
  int ret;
  uint64_t exp, val;
  const uint64_t lval = self | XND_ULOCK_WAITERS;

  for (;;)
    {
      val = xnd_atomic_load_relaxed (&ul->ul_val);
      if (val == 0)
        {
          if (__xnd_likely
              (xnd_atomic_cmpxchg_weak_acquire (&ul->ul_val, 0, lval)))
            return;
          continue;
        }

      if ((val & XND_ULOCK_WAITERS) == 0)
        {
          exp = val;
          val |= XND_ULOCK_WAITERS;
          if (!xnd_atomic_cmpxchg_weak_acquire (&ul->ul_val, exp, val))
            continue;
        }

      ret = __ulock_wait (XND_ULOCK_WAIT_OP, &ul->ul_val, val, 0);
      if (__xnd_unlikely (ret < 0))
        {
          if (ret != -EINTR && ret != -EFAULT)
            {
              xnd_ulock_panic ("__ulock_wait", -ret);
              unreachable ();
            }
        }
    }
}

void
xnd_ulock_wake (struct xnd_ulock *ul)
{
  int ret;

  do
    ret = __ulock_wake (XND_ULOCK_WAKE_OP, &ul->ul_val, 0);
  while (ret == -EINTR);

  if (__xnd_unlikely (ret < 0))
    {
      if (ret != -ENOENT)
        xnd_ulock_panic ("__ulock_wake", -ret);
    }
}
