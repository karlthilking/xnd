/* workq.c */
#include <errno.h>
#include <libproc.h>
#include <pthread.h>
#include <string.h>
#include <sys/sysctl.h>

#include "workq.h"
#include "thread_info.h"
#include "tls.h"
#include "wrappers/wq_wrappers.h"
#include "xnd_lib.h"
#include "pid/pid.h"
#include "util/env.h"

static void workq_reqthreads (int, pthread_priority_t, bool);
static void workq_setup (struct workq_dispatch_config *);
static void workq_thread_restore (void) __noreturn;

/* defined in thread_info.c */
extern pthread_key_t thread_self_key;
extern struct thread_list thread_list;

struct workqueue workq =
  {
    .wq_cfg =
    {
      .wdc_version = WORKQ_DISPATCH_CONFIG_VERSION,
      .wdc_flags = 0,
      .wdc_queue_serialno_offs = 0,
      .wdc_queue_label_offs = 0,
    },
    .wq_rwlock = PTHREAD_RWLOCK_INITIALIZER,
    .wq_flags = 0,
  };

void
workq_lck_wrlock (void)
{
  xpthread_rwlock_wrlock (&workq.wq_rwlock);
}

void
workq_lck_rdlock (void)
{
  xpthread_rwlock_rdlock (&workq.wq_rwlock);
}

void
workq_lck_unlock (void)
{
  xpthread_rwlock_unlock (&workq.wq_rwlock);
}

static void
workq_reqthreads (int nthreads, pthread_priority_t pp, bool cooperative)
{
  int op, ret;

  op = (cooperative ? WQOPS_QUEUE_REQTHREADS2 : WQOPS_QUEUE_REQTHREADS);
  ret = __workq_kernreturn (op, NULL, nthreads, (int) pp);
  if (ret != 0)
    xnd_panic ("__workq_kernreturn: %s\n", strerror (errno));
}

/*
 * workq_setup
 *  Register workqueue internals with the kernel and prepare the kernel
 *  before requesting workqueue threads.
 */
static void
workq_setup (struct workq_dispatch_config *cfg)
{
  int ret;

  ret = __workq_kernreturn (WQOPS_SETUP_DISPATCH, cfg, sizeof (*cfg), 0);
  if (ret != 0)
    xnd_panic ("__workq_kernreturn: %s\n", strerror (errno));

  if (__workq_open () != 0)
    xnd_panic ("__workq_open: %s\n", strerror (errno));
}

void
workq_thread_prepare (void)
{
  int err;
  sigset_t mask;
  struct thread_info *t;

  if ((READ_ONCE (workq.wq_flags) & WQ_RESTORING) != 0)
    {
      workq_thread_restore ();
      unreachable ();
    }

  if (__xnd_likely (thread_self () != NULL))
    {
      /*
       * workq_lck was acquired for reading in workq_thread_return_hook,
       * release the read lock before before resuming.
       */
      workq_lck_unlock ();
      return;
    }

  t = thread_init (NULL, NULL);
  t->ti_self = pthread_self ();
  t->ti_kport = mach_thread_self ();
  t->ti_tid = __thread_selfid ();

  t->ti_wq_thread = 1;
  t->ti_joinable = false;

  /*
   * Make sure checkpoint signal is not masked, and enable sending
   * signals for this workqueue thread.
   */
  mask = sigmask (env_get_ckpt_signal ());
  xpthread_sigmask (SIG_UNBLOCK, &mask, NULL);
  xnd_assert (__pthread_workqueue_setkill (1) == 0);

  xpthread_setspecific (thread_self_key, t);
  thread_list_add_self (t);
  xnd_atomic_store (&t->ti_state, TS_RUNNING, release);

  trace_workq_thread_start (t);
}

static void
workq_thread_restore (void)
{
  struct thread_info *t, *self = NULL;

  thread_list_acquire ();
  TAILQ_FOREACH (t, &thread_list, ti_entry)
    {
      if (t->ti_wq_thread && t->ti_wq_free)
        {
          self = t, self->ti_wq_free = 0;
          break;
        }
    }
  thread_list_release ();

  if (__xnd_unlikely (self == NULL))
    {
      xnd_error ("less free workqueue threads than expected");
      xnd_abort ();
    }

  thread_restart (self);
  unreachable ();
}

int
workq_reqthreads_hook (struct workq_kernreturn_args *args)
{
  int ret;

  workq_lck_rdlock ();
  ret = __workq_kernreturn (args->op, args->arg2, args->arg3, args->arg4);
  workq_lck_unlock ();

  return ret;
}

void
workq_thread_return_hook (struct workq_kernreturn_args *args)
{
  workq_lck_rdlock ();
  __workq_kernreturn (args->op, args->arg2, args->arg3, args->arg4);
  unreachable ();
}

/* the workq lock should be held for writing before calling
   workq_setup_callback */
void
workq_setup_callback (const struct pthread_workqueue_config *cfg)
{
  int ret;
  sigset_t mask;

  /* save workqueue config parameters, we will need them when
     re-registering the workqueue with the kernel on restart */
  workq.wq_cfg.wdc_queue_serialno_offs = cfg->queue_serialno_offs;
  workq.wq_cfg.wdc_queue_label_offs = cfg->queue_label_offs;

  /* ensure that workqueue threads will be able to receive our
     checkpoint signal when user threads are being suspended */
  mask = sigmask (env_get_ckpt_signal ());
  ret = __bsdthread_ctl (BSDTHREAD_CTL_WORKQ_ALLOW_SIGMASK, mask, 0, 0);
  if (ret != 0)
    xnd_panic ("__bsdthread_ctl: %s\n", strerror (errno));

  workq.wq_flags |= WQ_SETUP;
}

void
workq_suspend (void)
{
  struct timespec ts = { 0 };

  workq_lck_wrlock ();
  if ((workq.wq_flags & WQ_SETUP) != 0)
    {
      ts.tv_nsec = 2 * wq_max_timer_interval_nsecs ();
      nanosleep (&ts, NULL);
    }
}

void
workq_resume (void)
{
  /* clear WQ_RESTORING */
  WRITE_ONCE (workq.wq_flags, (workq.wq_flags & ~WQ_RESTORING));
  workq_lck_unlock ();
}

/*
 * workq_restore
 *  Re-register the workqueue with the kernel and request one workqueue
 *  thread for each workqueue threaed that was checkpointed.
 *
 *  workq.wq_lck is held at the time of workq_restore () from the
 *  acquire in workq_suspend ().
 */
void
workq_restore (void)
{
  uint16_t flags;
  struct thread_info *t;
  pthread_priority_t pp;

  /* nothing to do */
  if ((workq.wq_flags & WQ_SETUP) == 0)
    return;

  /* set WQ_RESTORING */
  WRITE_ONCE (workq.wq_flags, (workq.wq_flags | WQ_RESTORING));
  xnd_compiler_barrier_acquire ();

  workq_setup (&workq.wq_cfg);
  pp = _pthread_qos_class_encode (QOS_CLASS_USER_INITIATED, 0, 0);

  thread_list_acquire ();
  TAILQ_FOREACH (t, &thread_list, ti_entry)
    {
      if (t->ti_wq_thread)
        {
          t->ti_wq_free = 1;
          workq_reqthreads (1, pp, false);
        }
    }
  thread_list_release ();
}
