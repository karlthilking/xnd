/* workqueue-defs.h */
#ifndef XND_WORKQUEUE_DEFS_H
#define XND_WORKQUEUE_DEFS_H

#include <mach/port.h>
#include <stdint.h>
#include <stddef.h>

#include "xnd/thread-priority-defs.h"

/* Definitions of workqueue, kevent and workloop related constants and
   types not available in public headers.

   Sources:
    xnu/bsd/pthread/workqueue_syscalls.h
    xnu/bsd/pthread/bsdthread_private.h
    libpthread/kern/kern_internal.h
    libpthread/private/pthread/workqueue_private.h */

/* value for nkevents passed to _pthread_wqthread () by the kernel when
   requesting for libpthread to have a workqueue thread exit */
#define WORKQ_EXIT_THREAD_NKEVENT (-1)

/* upcall flags passed to _pthread_wqthread () by the kernel; the low
   bits (WQ_FLAG_THREAD_PRIO_MASK) hold the thread qos when
   WQ_FLAG_THREAD_PRIO_QOS is set, or the sched priority when
   WQ_FLAG_THREAD_PRIO_SCHED is set */
#define WQ_FLAG_THREAD_PRIO_SCHED     0x00008000
#define WQ_FLAG_THREAD_PRIO_QOS       0x00004000
#define WQ_FLAG_THREAD_PRIO_MASK      0x00000fff

#define WQ_FLAG_THREAD_OVERCOMMIT     0x00010000
#define WQ_FLAG_THREAD_REUSE          0x00020000
#define WQ_FLAG_THREAD_NEWSPI         0x00040000
#define WQ_FLAG_THREAD_KEVENT         0x00080000
#define WQ_FLAG_THREAD_EVENT_MANAGER  0x00100000
#define WQ_FLAG_THREAD_TSD_BASE_SET   0x00200000
#define WQ_FLAG_THREAD_WORKLOOP       0x00400000
#define WQ_FLAG_THREAD_OUTSIDEQOS     0x00800000
#define WQ_FLAG_THREAD_COOPERATIVE    0x01000000

/* workq opcode for __workq_kernreturn (int op, ...) */
#define WQOPS_THREAD_RETURN              0x004
#define WQOPS_QUEUE_NEWSPISUPP           0x010
#define WQOPS_QUEUE_REQTHREADS           0x020
#define WQOPS_QUEUE_REQTHREADS2          0x030
#define WQOPS_THREAD_KEVENT_RETURN       0x040
#define WQOPS_SET_EVENT_MANAGER_PRIORITY 0x080
#define WQOPS_THREAD_WORKLOOP_RETURN     0x100
#define WQOPS_SHOULD_NARROW              0x200
#define WQOPS_SETUP_DISPATCH             0x400

/* __bsdthread_ctl commands */
#define BSDTHREAD_CTL_WORKQ_ALLOW_KILL    0x1000
#define BSDTHREAD_CTL_WORKQ_ALLOW_SIGMASK 0x4000

/* workqueue config for pthread_workqueue_setup () */
#define PTHREAD_WORKQUEUE_CONFIG_VERSION               2
#define PTHREAD_WORKQUEUE_CONFIG_MIN_SUPPORTED_VERSION 1
#define PTHREAD_WORKQUEUE_CONFIG_SUPPORTED_FLAGS       0
struct pthread_workqueue_config
{
  uint32_t flags;
  uint32_t version;
  void (*kevent_cb) (void **events, int *nevents);
  void (*workloop_cb) (uint64_t *id, void **events, int *nevents);
  void (*workq_cb) (pthread_priority_t pp);
  uint64_t queue_serialno_offs;
  uint64_t queue_label_offs;
};

/* workqueue config for __workq_kernreturn (WQOPS_SETUP_DISPATCH, ...) */
#define WORKQ_DISPATCH_CONFIG_VERSION        2
#define WORKQ_DISPATCH_MIN_SUPPORTED_VERSION 1
#define WORKQ_DISPATCH_SUPPORTED_FLAGS       0
struct workq_dispatch_config
{
  uint32_t wdc_version;
  uint32_t wdc_flags;
  uint64_t wdc_queue_serialno_offs;
  uint64_t wdc_queue_label_offs;
} __attribute__ ((packed, aligned (4)));

#define WQ_KEVENT_LIST_LEN  16 // WORKQ_KEVENT_EVENT_BUFFER_LEN
#define WQ_KEVENT_DATA_SIZE (32 * 1024)

/* kqueue_workloop_ctl commands */
#define KQ_WORKLOOP_CREATE  0x01
#define KQ_WORKLOOP_DESTROY 0x02

/* indicate which fields of kq_workloop_create params are valid */
#define KQ_WORKLOOP_CREATE_SCHED_PRI         0x01
#define KQ_WORKLOOP_CREATE_SCHED_POL         0x02
#define KQ_WORKLOOP_CREATE_CPU_PERCENT       0x04
#define KQ_WORKLOOP_CREATE_WORK_INTERVAL     0x08
#define KQ_WORKLOOP_CREATE_WITH_BOUND_THREAD 0x10

struct kqueue_workloop_params
{
  int kqwlp_version;
  int kqwlp_flags;
  uint64_t kqwlp_id;
  int kqwlp_sched_pri;
  int kqwlp_sched_pol;
  int kqwlp_cpu_percent;
  int kqwlp_cpu_refillms;
  mach_port_name_t kqwl_wi_port;
} __attribute__ ((packed));

extern int __workq_open (void);
extern int __workq_kernreturn (int, void *, int, int);
extern int __kqueue_workloop_ctl (uintptr_t, uint64_t, void *, size_t);
extern int pthread_workqueue_setup (struct pthread_workqueue_config *, size_t);

#endif /* XND_WORKQUEUE_DEFS_H */
