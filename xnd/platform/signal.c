/* signal.c */
#include <errno.h>
#include <signal.h>
#include <ucontext.h>

#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/platform/signal.h"

extern int __sigwait (const sigset_t *, int *);
extern int __sigreturn (ucontext_t *, int, uintptr_t);
extern int __sigaction (int, struct __sigaction *, struct __sigaction *);

static void xnd_sigreturn (ucontext_t *, int, uintptr_t) __noreturn;
static void xnd_sigtramp (union __sigaction_u, int, int, siginfo_t *,
                          ucontext_t *, uintptr_t) __noreturn;

/*
 * xnd_sigreturn:
 *  Re-sign signal frame user context and pac-signed return addresses
 *  before calling __sigreturn.
 */
static void
xnd_sigreturn (ucontext_t *uctx, int ctxstyle, uintptr_t token)
{
  u64 *fp = (u64 *) get_ucontext_fp (uctx);

  ptrauth_patch_siguctx (uctx);
  ptrauth_resign_frames (fp);

  __sigreturn (uctx, ctxstyle, token);
  xnd_panic ("fatal error: __sigreturn fallthrough\n");

  unreachable ();
}

/*
 * xnd_sigtramp:
 *  Invoke user signal handler and use xnd_sigreturn to restore
 *  current thread through PAC-aware return path.
 */
static void
xnd_sigtramp (union __sigaction_u __sigaction_u, int sigstyle, int sig,
              siginfo_t *sinfo, ucontext_t *uctx, uintptr_t token)
{
  sa_sigaction (sig, sinfo, uctx);
  xnd_sigreturn (uctx, UC_FLAVOR, token);
  unreachable ();
}

int
xnd_sigaction (int sig, const struct sigaction *nsv, struct sigaction *osv)
{
  int ret;
  struct __sigaction sa, osa;
  struct __sigaction *sap;

  if (sig <= 0 || sig >= NSIG)
    {
      errno = EINVAL;
      return -1;
    }

  if (nsv != NULL && (sig == SIGSTOP || sig == SIGKILL))
    {
      errno = EINVAL;
      return -1;
    }

  sap = (struct __sigaction *) 0;
  if (nsv != NULL)
    {
      sa.sa_handler = nsv->sa_handler;
      sa.sa_tramp = (void *) xnd_sigtramp;
      sa.sa_mask = nsv->sa_mask;
      sa.sa_flags = nsv->sa_flags;
      sa.sa_flags &= ~SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP;
      sap = &sa;
    }

  ret = __sigaction (sig, sap, &osa);
  if (osv != NULL && ret == 0)
    {
      osv->sa_handler = osa.sa_handler;
      osv->sa_mask = osa.sa_mask;
      osv->sa_flags = osa.sa_flags;
    }

  /*
	 * FIXME:
	 *  Does __sigaction return an error number or -1??
	 */
  if (ret > 0)
    {
      errno = ret;
      ret = -1;
    }

  return ret;
}
