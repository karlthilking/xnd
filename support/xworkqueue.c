/* xworkqueue.c */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "workqueue.h"
#include "xworkqueue.h"
#include "common/compiler.h"

#define __xworkq(op, ...)                    \
  do {                                       \
    int __err = workq_##op (__VA_ARGS__);    \
    if (__err != 0)                          \
      {                                      \
        workq_error (STRINGIFY (op), __err); \
        abort ();                            \
      }                                      \
  } while (0)

static void
workq_error (const char *msg, int errnum)
{
  char buf[256];

  if (isatty (STDERR_FILENO))
    {
      strerror_r (errnum, buf, sizeof (buf));
      dprintf (STDERR_FILENO, "%s: %s\n", msg, buf);
    }
}

void
xworkq_setup (struct pthread_workqueue_config *cfg)
{
  __xworkq (setup, cfg);
}

void
xworkq_setup_workloop (void (*workq_cb) (pthread_priority_t),
                       void (*kevent_cb) (void **, int *),
                       void (*workloop_cb) (uint64_t *, void **, int *))
{
  __xworkq (setup_workloop, workq_cb, kevent_cb, workloop_cb);
}

void
xworkq_setup_kevent (void (*workq_cb) (pthread_priority_t),
                     void (*kevent_cb) (void **, int *))
{
  __xworkq (setup_kevent, workq_cb, kevent_cb);
}

void
xworkq_setup_worker (void (*workq_cb) (pthread_priority_t))
{
  __xworkq (setup_worker, workq_cb);
}

void
xworkq_reqthreads (int nthreads, pthread_priority_t pp, bool cooperative)
{
  __xworkq (reqthreads, nthreads, pp, cooperative);
}

void
xworkq_set_event_mgr_pri (pthread_priority_t pp)
{
  __xworkq (set_event_mgr_pri, pp);
}

void
xworkq_kevent_submit (const struct kevent_qos_s *events, int nevents)
{
  __xworkq (kevent_submit, events, nevents);
}

void
xworkq_allow_kill (int enable)
{
  __xworkq (allow_kill, enable);
}

void
xworkq_allow_sigmask (sigset_t mask)
{
  __xworkq (allow_sigmask, mask);
}
