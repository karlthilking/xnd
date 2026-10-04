/* signal_wrappers.c */
#include "signal_wrappers.h"
#include "xnd/xnd.h"
#include "xnd/pac.h"
#include "xnd/xnd_lib.h"
#include "xnd/interpose.h"
#include "xnd/thread_info.h"
#include "xnd/util/env.h"
#include "xnd/platform/signal.h"
#include "xnd/platform/ucontext/ucontext.h"

#include <ucontext.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <err.h>

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
  int ckptsig = env_get_ckpt_signal ();

  if (sigismember (set, ckptsig))
    {
      xnd_warn ("can't mask signal: %s\n", strsignal (ckptsig));
      sigdelset (set, ckptsig);
    }
}

extern bool _xnd_is_threaded;

int
sigprocmask_hook (int how, const sigset_t *set, sigset_t *oset)
{
  sigset_t mask, *maskp = NULL;

  /*
	 * Only the calling thread and the checkpoint thread are running.
	 * Translate sigprocmask to pthread_sigmask.
	 */
  if (!xnd_atomic_load (&_xnd_is_threaded, relaxed))
    return pthread_sigmask_hook (how, set, oset);

  if (set != NULL)
    {
      mask = *set;
      maskp = &mask;
    }

  /* Need to remove checkpoint signal from mask. */
  if (set != NULL && (how == SIG_SETMASK || how == SIG_BLOCK)
      && get_xnd_state () != XND_ABORTING)
    sigmask_clean (maskp);

  return sigprocmask (how, maskp, oset);
}

int
pthread_sigmask_hook (int how, const sigset_t *set, sigset_t *oset)
{
  sigset_t mask, *maskp = NULL;

  if (set != NULL)
    {
      mask = *set;
      maskp = &mask;
    }

  if (set != NULL && (how == SIG_SETMASK || how == SIG_BLOCK)
      && get_xnd_state () != XND_ABORTING)
    sigmask_clean (maskp);

  return pthread_sigmask (how, maskp, oset);
}

INTERPOSE (signal_hook, signal);
INTERPOSE (sigaction_hook, sigaction);
INTERPOSE (sigprocmask_hook, sigprocmask);
INTERPOSE (pthread_sigmask_hook, pthread_sigmask);
