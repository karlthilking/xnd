/* xnd-ulock.c */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/compiler.h"
#include "xnd-ulock.h"

static void xnd_ulock_panic (const char *, int) __cold __noreturn;

static void
xnd_ulock_panic (const char *msg, int errnum)
{
  const char fmt[] = "%s: %s: %s\n";

  if (isatty (2))
    dprintf (2, fmt, __FILE_NAME__, msg, strerror (errnum));

  exit (1);
}

void
xnd_ulock_wait (struct xnd_ulock *ul)
{
  int ret;
  uint32_t val;

  for (int i = 0; i < 3; i++)
    {
      do
        {
          xnd_atomic_yield ();
          val = xnd_atomic_load_relaxed (&ul->ul_val);
          if (__xnd_unlikely (val == 2))
            goto wait;
        }
      while (val != 0);
      if (xnd_atomic_cmpxchg_weak_acquire (&ul->ul_val, 0, 1))
        return;
    }

  do
    {
wait:
      ret = __ulock_wait (XND_ULOCK_WAIT_OP, &ul->ul_val, 2, 0);
      if (__xnd_unlikely (ret < 0))
        {
          switch (-ret)
            {
            case EINTR:
            case EFAULT:
            case ENOMEM:
            case EOWNERDEAD:
              break;
            default:
              xnd_ulock_panic ("__ulock_wait", -ret);
            }
        }
    }
  while (xnd_atomic_xchg_acquire (&ul->ul_val, 2) != 0);
}

void
xnd_ulock_wake (struct xnd_ulock *ul)
{
  int ret;

wake:
  ret = __ulock_wake (XND_ULOCK_WAKE_OP, &ul->ul_val, 0);
  if (__xnd_unlikely (ret < 0))
    {
      switch (-ret)
        {
        case EINTR:
          goto wake;
        case ENOENT:
          break;
        default:
          xnd_ulock_panic ("__ulock_wake: %s\n", -ret);
        }
    }
}
