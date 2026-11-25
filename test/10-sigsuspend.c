/* 10-sigsuspend.c */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/compiler.h"
#include "common/xthread.h"

#define xsignal(sig, handler)                   \
  ({                                            \
    int __sig = (sig);                          \
    void (*__handler) (int) = (handler);        \
    void (*__ret) (int) = NULL;                 \
    __ret = signal (__sig, __handler);          \
    if (__ret == SIG_ERR)                       \
      {                                         \
        perror ("signal");                      \
        abort ();                               \
      }                                         \
    __ret;                                      \
  })

struct thread_args
{
  int caught_sig;
  int caught_seq;
  int iters;
  bool set_mask;
  uint32_t counts[NSIG];
  sigset_t mask;
  sigset_t suspend_mask;
  pthread_cond_t cond;
  pthread_mutex_t lock;
};

static _Thread_local int caught_sig = -1;

static const char *
strsignal_name (int sig)
{
#define SIGNAL_LIST(X)                                                  \
  X (SIGHUP) X (SIGINT) X (SIGQUIT) X (SIGILL) X (SIGTRAP) X (SIGABRT)  \
    X (SIGEMT) X (SIGFPE) X (SIGKILL) X (SIGBUS) X (SIGSEGV) X (SIGSYS) \
    X (SIGPIPE) X (SIGALRM) X (SIGTERM) X (SIGURG) X (SIGSTOP)          \
    X (SIGTSTP) X (SIGCONT) X (SIGCHLD) X (SIGTTIN) X (SIGTTOU)         \
    X (SIGIO) X (SIGXCPU) X (SIGXFSZ) X (SIGVTALRM) X (SIGPROF)         \
    X (SIGWINCH) X (SIGINFO) X (SIGUSR1) X (SIGUSR2)

  const char *signal_names[NSIG] =
    {
#define X(sig) [sig] = #sig,
      SIGNAL_LIST (X)
#undef X
    };

#undef SIGNAL_LIST

  return sig > 0 && sig < NSIG ? signal_names[sig] : NULL;
}

static void
signal_handler (int sig)
{
  caught_sig = sig;
}

static void *
sigsuspend_thread (void *arg)
{
  int ret;
  struct thread_args *args = (struct thread_args *) arg;

  xpthread_sigmask (SIG_SETMASK, &args->mask, NULL);

  xpthread_mutex_lock (&args->lock);
  args->set_mask = true;
  xpthread_cond_signal (&args->cond);
  xpthread_mutex_unlock (&args->lock);

  for (int i = 0; i < args->iters; i++)
    {
      ret = sigsuspend (&args->suspend_mask);
      assert (ret == -1 && errno == EINTR);

      /* Keep track of how many times each signal was received. */
      args->counts[caught_sig]++;

      xpthread_mutex_lock (&args->lock);
      args->caught_seq = i;
      args->caught_sig = caught_sig;
      xpthread_cond_signal (&args->cond);
      xpthread_mutex_unlock (&args->lock);
    }

  pthread_exit (NULL);
}

static void
report_signal (int iter, int sig)
{
  const char *name = strsignal_name (sig);
  printf ("(%09d) Signal: %s\t(%s)\n", iter, name, strsignal (sig));
}

int
main (int argc, char *argv[])
{
  int err, sig, iters, log_every = 100000;
  pthread_t thread;
  sigset_t emptyset, fullset;
  uint32_t sig_counts[NSIG] = {0};
  const int sigs[] = { SIGSYS, SIGURG, SIGXCPU, SIGPROF, SIGUSR1 };

  iters = argc > 1 ? atoi (argv[1]) : 2500000;
  sigemptyset (&emptyset);
  sigfillset (&fullset);

  struct thread_args args =
    {
      .caught_sig = -1,
      .caught_seq = -1,
      .iters = iters,
      .set_mask = false,
      .counts = {0},
      .mask = fullset,
      .suspend_mask = emptyset,
    };

  xpthread_cond_init (&args.cond, NULL);
  xpthread_mutex_init (&args.lock, NULL);

  for (int i = 0; i < countof (sigs); i++)
    xsignal (sigs[i], signal_handler);

  xpthread_create (&thread, NULL, sigsuspend_thread, (void *) &args);

  xpthread_mutex_lock (&args.lock);
  while (! args.set_mask)
    xpthread_cond_wait (&args.cond, &args.lock);
  xpthread_mutex_unlock (&args.lock);

  for (int i = 0; i < iters; i++)
    {
      sig = sigs[i % countof (sigs)];
      err = pthread_kill (thread, sig);
      if (err != 0)
        {
          fprintf (stderr, "pthread_kill: %s\n", strerror (err));
          abort ();
        }

      xpthread_mutex_lock (&args.lock);
      while (args.caught_seq != i)
        xpthread_cond_wait (&args.cond, &args.lock);

      sig = args.caught_sig;
      sig_counts[sig]++;

      if (i % log_every == 0)
        report_signal (i, sig);

      xpthread_mutex_unlock (&args.lock);
    }

  xpthread_join (thread, NULL);
  for (int i = 1; i < NSIG; i++)
    assert (args.counts[i] == sig_counts[i]);

  exit (0);
}
