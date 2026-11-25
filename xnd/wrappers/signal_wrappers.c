/* signal_wrappers.c */
#include "signal_wrappers.h"
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/xnd_lib.h"
#include "xnd/interpose.h"
#include "xnd/thread_info.h"
#include "xnd/util/env.h"
#include "xnd/wrappers/pthread_wrappers.h"
#include "xnd/platform/signal.h"
#include "xnd/platform/ucontext/ucontext.h"

#include <ucontext.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <err.h>

extern uint64_t xnd_epoch;
extern bool _xnd_is_threaded;

static struct sigaction sa_table[NSIG];

void
sig_state_save (void)
{
  int ret, sig;
  struct sigaction *act = NULL;

  for (sig = 1; sig < NSIG; sig++)
    {
      if (sig == SIGKILL || sig == SIGSTOP)
        continue;
      act = &sa_table[sig];
      ret = xnd_sigaction (sig, NULL, act);
      if (ret != 0)
        {
          xnd_perror ("xnd_sigaction");
          bzero (act, sizeof (*act));
        }
    }

  sig = env_get_ckpt_signal ();
  if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP)
    xnd_panic ("bad checkpoint signal: %s\n", strsignal (sig));

  act = &sa_table[sig];
  if (act->sa_sigaction != thread_sighandler)
    {
      xnd_warn ("checkpoint signal action corrupt\n");
      sigfillset (&act->sa_mask);
      act->sa_flags = SA_SIGINFO | SA_RESTART;
      act->sa_sigaction = thread_sighandler;
    }
}

void
sig_state_restore (void)
{
  int ret, sig;
  struct sigaction *act = NULL;

  sig = env_get_ckpt_signal ();
  if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP)
    xnd_panic ("bad checkpoint signal: %s\n", strsignal (sig));

  act = &sa_table[sig];
  if (act->sa_sigaction != thread_sighandler)
    {
      xnd_warn ("checkpoint signal action corrupt\n");
      sigfillset (&act->sa_flags);
      act->sa_flags = SA_SIGINFO | SA_RESTART;
      act->sa_sigaction = thread_sighandler;
    }

  for (sig = 1; sig < NSIG; sig++)
    {
      if (sig == SIGKILL || sig == SIGSTOP)
        continue;
      act = &sa_table[sig];
      ret = xnd_sigaction (sig, act, NULL);
      if (ret != 0)
        xnd_perror ("xnd_sigaction");
    }
}

sig_t
signal_hook (int sig, sig_t handler)
{
  int ret;
  struct sigaction act, oact;

  if (sig == env_get_ckpt_signal ())
    {
      xnd_warn ("%s is reserved\n", strsignal (sig));
      return SIG_ERR;
    }

  if (handler == SIG_DFL || handler == SIG_IGN)
    return signal (sig, handler);

  /*
         * Register user signal handlers with xnd_sigaction so we can
         * control the signal trampoline function
         */
  sigemptyset (&act.sa_mask);
  act.sa_flags = SA_RESTART;
  act.sa_handler = handler;

  /*
	 * Return previous action on success, SIG_ERR on failure.
	 * xnd_sigaction will set errno and fill in oact.
	 */
  ret = xnd_sigaction (sig, &act, &oact);
  return (ret == 0 ? oact.sa_handler : SIG_ERR);
}

/*
 * sigaction_hook:
 *  Block user threads from establishing a new handler for the checkpoint
 *  signal or observing the disposition for the checkpoint signal. If
 *  a user thread is registering a new handler, use xnd_sigaction to
 *  setup xnd_sigtramp as the signal trampoline.
 */
int
sigaction_hook (int sig, const struct sigaction *act, struct sigaction *oact)
{
  int ret, ckptsig = env_get_ckpt_signal ();

  /*
	 * If a user thread is querying information about the
	 * checkpoint signal, hide the associated signal state.
         */
  if (act == NULL)
    {
      ret = sigaction (sig, NULL, oact);
      if (sig == ckptsig)
        {
          sigemptyset (&oact->sa_mask);
          oact->sa_flags = 0;
          oact->sa_handler = SIG_DFL;
        }
      return ret;
    }

  /*
	 * Don't let a user thread establish a different signal
	 * dispostion for the checkpoint signal.
	 */
  if (sig == ckptsig)
    {
      xnd_warn ("reserved signal: %s\n", strsignal (sig));
      return -1;
    }

  if (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN)
    return sigaction (sig, act, oact);

  /*
	 * If a user thread is registering their own signal handler,
	 * route the registery through xnd_sigaction so we can use
	 * our own signal trampoline (xnd_sigtramp).
	 */
  return xnd_sigaction (sig, act, oact);
}

static inline void
sigmask_clean (sigset_t *set)
{
  int sig = env_get_ckpt_signal ();

  if (sigismember (set, sig))
    sigdelset (set, sig);
}

int
sigprocmask_hook (int how, const sigset_t *set, sigset_t *oset)
{
  int ret;
  sigset_t mask;

  /* Only the calling thread and the checkpoint thread are running.
     Translate sigprocmask to pthread_sigmask. */
  if (!xnd_atomic_load_relaxed (&_xnd_is_threaded))
    return pthread_sigmask_hook (how, set, oset);

  if (set != NULL)
    {
      mask = *set;
      /* don't bother cleaning the mask and printing a warning message
         if abort was called (and sigprocmask is being called with
         a full mask) */
      if (get_xnd_state () != XND_ABORTING)
        {
          if (how == SIG_SETMASK || how == SIG_BLOCK)
            sigmask_clean (&mask);
        }
      ret = sigprocmask (how, &mask, oset);
    }
  else
    ret = sigprocmask (how, NULL, oset);

  return ret;
}

/* XXX Should pthread_sigmask hook report that the checkpoint
   signal is masked in oset if the user previously tried to mask
   the checkpoint signal? */
int
pthread_sigmask_hook (int how, const sigset_t *set, sigset_t *oset)
{
  int ret;
  sigset_t mask;

  if (set != NULL)
    {
      mask = *set;
      if (get_xnd_state () != XND_ABORTING)
        {
          if (how == SIG_SETMASK || how == SIG_BLOCK)
            sigmask_clean (&mask);
        }
      ret = pthread_sigmask (how, &mask, oset);
    }
  else
    ret = pthread_sigmask (how, NULL, oset);

  return ret;
}

/* The kernel's __sigwait blocks all signals not in set as well as
   SIGKILL and SIGSTOP, so the checkpoint signal can not be delivered
   while a thread is inside sigwait.

   In order to allow for the checkpoint signal to be delivered,
   include the checkpoint signal in the sigwait set and re-raise the
   checkpoint signal if it is sent. The hook only returns when one of
   the specified signals (not the checkpoint signal) is received. */
static int
sigwait_hook (const sigset_t *uset, int *usig)
{
  int ret, ckptsig, sig = 0;
  sigset_t set, oset, ckptmask;
  struct thread_info *t = thread_self ();

  if (uset == NULL)
    return EINVAL;

  /* Get checkpoint signal and make signal mask with only the
     checkpoint signal */
  ckptsig = env_get_ckpt_signal ();
  sigemptyset (&ckptmask);
  sigaddset (&ckptmask, ckptsig);

  /* Include checkpoint signal in sigwait set. */
  set = *uset;
  sigaddset (&set, ckptsig);

  /* Block the checkpoint signal and save the current signal mask
     before calling sigwait. */
  unsafe_enter ();
  xpthread_sigmask (SIG_BLOCK, &ckptmask, &oset);
  unsafe_exit ();

  do
    {
      ret = sigwait (&set, &sig);
      /* If the checkpoint signal was received, unblock it and raise
         the checkpoint signal for the current thread now. */
      if (ret == 0 && sig == ckptsig)
        {
          xpthread_sigmask (SIG_UNBLOCK, &ckptmask, NULL);

          /* Call __pthread_kill directly to bypass the __pthread_kill
             interposer in pthread_wrappers.c. */
          ret = __pthread_kill (t->ti_kport, ckptsig);
          if (ret != 0)
            {
              xnd_strerror ("__pthread_kill", errno);
              xnd_abort ();
            }

          xpthread_sigmask (SIG_BLOCK, &ckptmask, NULL);
        }
    }
  while (ret == 0 && sig == ckptsig);

  if (ret == 0 && usig != NULL)
    *usig = sig;

  xpthread_sigmask (SIG_SETMASK, &oset, NULL);
  return ret;
}
INTERPOSE (sigwait_hook, sigwait);

/* libxnd forces user threads to leave the checkpoint signal unblocked
   for the checkpoint signal handler, so sigsuspend will not interfere
   with the checkpoint signal being received.

   However, sigsuspend_hook should be sure that the checkpoint signal
   is not is the sigsuspend mask, and not return to the user until a
   signal other than the checkpoint signal is received. */
static int
sigsuspend_hook (const sigset_t *umask)
{
  int ret, ckptsig;
  uint64_t sigcnt;
  sigset_t mask, omask, ckptmask;
  struct thread_info *self = thread_self ();

  xnd_assert (self != NULL);
  ckptsig = env_get_ckpt_signal ();

  /* Ensure checkpoint signal is not in the sigsuspend mask. */
  mask = *umask;
  sigdelset (&mask, ckptsig);

  /* Mask the checkpoint signal. This is necessary so that the check
     after returning from sigsuspend to see if we resumed/restart after
     a checkpoint is atomic. */
  ckptmask = sigmask (ckptsig);
  unsafe_enter ();
  xpthread_sigmask (SIG_BLOCK, &ckptmask, &omask);
  unsafe_exit ();

  sigcnt = READ_ONCE (self->ti_sigcnt);
  for (;;)
    {
      ret = sigsuspend (&mask);
      xnd_assert (ret == -1 && errno == EINTR);

      /* If signal catch count increased, then a signal other than the
         checkpoint signal was caught. */
      if (READ_ONCE (self->ti_sigcnt) > sigcnt)
        break;
    }

  /* Restore old signal mask. This also guarantees that the checkpoint
     signal will no longer be blocked. */
  xpthread_sigmask (SIG_SETMASK, &omask, NULL);
  return -1;
}
INTERPOSE (sigsuspend_hook, sigsuspend);

INTERPOSE (signal_hook, signal);
INTERPOSE (sigaction_hook, sigaction);
INTERPOSE (sigprocmask_hook, sigprocmask);
INTERPOSE (pthread_sigmask_hook, pthread_sigmask);
