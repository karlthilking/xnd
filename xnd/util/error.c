/* error.c */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "xnd/util/error.h"

extern int __pthread_workqueue_setkill (int);

static struct xnd_error_ops error_ops =
  {
    .vdprintf = vdprintf,
    .vsnprintf = vsnprintf,
  };

void
xnd_error_set_io (const struct xnd_error_ops *ops)
{
  error_ops.vdprintf = ops->vdprintf;
  error_ops.vsnprintf = ops->vsnprintf;
}

static int
error_dprintf (int fd, const char *fmt, ...)
{
  int ret;
  va_list ap;

  va_start (ap, fmt);
  ret = (*error_ops.vdprintf) (fd, fmt, ap);
  va_end (ap);

  return ret;
}

void
xnd_error_internal (const char *color, const char *file,
		    const char *func, int line,
		    const char *fmt, ...)
{
  char msg[512];
  va_list ap;

  va_start (ap, fmt);
  (void) (*error_ops.vsnprintf) (msg, sizeof (msg), fmt, ap);
  va_end (ap);
  (void) error_dprintf (STDERR_FILENO, "%s[xnd:%s:%s:%d] %s%s",
                        color, file, func, line, msg, ANSI_RESET);
}

void
xnd_perror_internal (const char *msg, int errnum,
		     const char *file, const char *func, int line)
{
  char buf[128];
  const char fmt[] = "%s: %s\n";

  (void) strerror_r (errnum, buf, sizeof (buf));
  xnd_error_internal (ANSI_RED, file, func, line, fmt, msg, buf);
}

void
xnd_psignal_internal (int sig, const char *msg, const char *file,
		      const char *func, int line)
{
  char buf[128];
  const char fmt[] = "%s: %s\n";

  xnd_assert (sig > 0 && sig < NSIG);
  (void) strsignal_r (sig, buf, sizeof (buf));
  xnd_error_internal (ANSI_RED, file, func, line, fmt, msg, buf);
}

void
xnd_assert_fail (const char *expr, const char *file,
		 const char *func, int line)
{
  const char fmt[] = "assertion failed: (%s)\n";

  xnd_error_internal (ANSI_RED, file, func, line, fmt, expr);
  xnd_abort ();
}

void
xnd_abort (void)
{
  sigset_t mask;

  /* Mask every signal but SIGABRT in the calling thread only (on
     Darwin, sigprocmask is per-thread like pthread_sigmask). Other
     threads keep running until the default SIGABRT action
     terminates the process. */
  sigfillset (&mask);
  sigdelset (&mask, SIGABRT);
  (void) pthread_sigmask (SIG_SETMASK, &mask, NULL);

  /* enable kill for a workqueue thread */
  (void) __pthread_workqueue_setkill (1);

  (void) signal (SIGABRT, SIG_DFL);
  (void) pthread_kill (pthread_self (), SIGABRT);

  /* fallback if abort failed */
  sigfillset (&mask);
  sigdelset (&mask, SIGILL);
  sigdelset (&mask, SIGTRAP);
  (void) pthread_sigmask (SIG_SETMASK, &mask, NULL);
  __builtin_trap ();

  __builtin_unreachable ();
}
