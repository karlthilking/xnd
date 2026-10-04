/* wqthread.h */
#ifndef XND_WQTHREAD_H
#define XND_WQTHREAD_H

#include "xnd/xnd.h"
#include "common/time.h"
#include "wrappers/wq_wrappers.h"

extern int __bsdthread_ctl (uintptr_t cmd, uintptr_t arg2,
			    uintptr_t arg3, uintptr_t arg4);
extern int __pthread_workqueue_setkill (int enable);
extern int _pthread_workqueue_allow_send_signals (int sig);

#define BSDTHREAD_CTL_WORKQ_ALLOW_KILL 		0x1000
#define BSDTHREAD_CTL_WORKQ_ALLOW_SIGMASK 	0x4000

#define trace_workq_thread_start(t)					\
  ({									\
    struct thread_info *__t = (t);					\
    printf ("%s: pthread=%p, kport=%u, tid=%llu\n",			\
	    __func__, __t->ti_self, __t->ti_kport, __t->ti_tid);	\
  })

#define workq_sysctl(type, name)			    \
  ({							    \
    int __ret;						    \
    type __val;						    \
    size_t __len = sizeof (__val);			    \
    __ret = sysctlbyname ((name), &__val, &__len, NULL, 0); \
    if (__xnd_unlikely (__ret == -1))				    \
      xnd_panic ("sysctlbyname: %s\n", strerror (errno));   \
    __val;						    \
  })

/*
 * kern.wq_stalled_window_usecs
 *  threshold for for distinguishing between busy and idle/blocked
 *  workqueue threads
 *
 * now - last blocked timestamp < wq_stalled_window
 *  thread is busy
 * now - last blocked timestamp >= wq_stalled_window
 *  thread is idle/blocked
 */
#define wq_stalled_window_usecs() \
  workq_sysctl (uint32_t, "kern.wq_stalled_window_usecs")
#define wq_stalled_window_nsecs() \
  ((uint64_t) wq_stalled_window_usecs () * NSEC_PER_USEC)

/*
 * kern.wq_reduce_pool_window_usecs
 *  maximum time that a workqueue thread can park in the kernel
 *  before the kernel will kill it to reduce the pool size
 */
#define wq_reduce_pool_window_usecs() \
  workq_sysctl (uint32_t, "kern.wq_reduce_pool_window_usecs")
#define wq_reduce_pool_window_nsecs() \
  ((uint64_t) wq_reduce_pool_window_usecs () * NSEC_PER_USEC)

/*
 * kern.wq_max_timer_interval_usecs
 *  maximum time that the kernel will stall before creating a workqueue
 *  thread for delayed creation.
 *
 *  for delayed thread creation:
 *   wq_stalled_window <= delay <= wq_max_timer_interval
 */
#define wq_max_timer_interval_usecs() \
  workq_sysctl (uint32_t, "kern.wq_max_timer_interval_usecs")
#define wq_max_timer_interval_nsecs() \
  ((uint64_t) wq_max_timer_interval_usecs () * NSEC_PER_USEC)

#define WQ_SETUP 	0x0001
#define WQ_RESTORING	0x0002

struct workqueue
{
  struct workq_dispatch_config wq_cfg;
  pthread_rwlock_t wq_rwlock;
  uint32_t wq_flags;
};

struct workq_kernreturn_args
{
  int op;
  void *arg2;
  int arg3;
  int arg4;
};

void workq_thread_prepare (void);

int workq_reqthreads_hook (struct workq_kernreturn_args *);
void workq_thread_return_hook (struct workq_kernreturn_args *) __noreturn;
int workq_setup_dispatch_hook (struct workq_kernreturn_args *);

void workq_suspend (void);
void workq_resume (void);
void workq_restore (void);

#endif /* XND_WQTHREAD_H */
