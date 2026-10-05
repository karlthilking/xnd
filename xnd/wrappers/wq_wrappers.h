/* wq_wrappers.h */
#ifndef WQ_WRAPPERS_H
#define WQ_WRAPPERS_H

#include "xnd/xnd.h"
#include <pthread/pthread.h>
#include <sys/qos.h>
#include <pthread/qos.h>

#if DEVELOPMENT || DEBUG
# define trace_workq_kernreturn(op, arg2, arg3, arg4)                  \
   printf ("__workq_kernreturn (%s, %p, %d, %d)\n", wqops_string (op), \
           (arg2), (arg3), (arg4));
# define trace_pthread_workqueue_setup(cfg)                          \
   printf ("pthread_workqueue_setup (version=%d, flags=%d, "         \
           "queue_serialno_offs=%llu, queue_label_offs=%llu\n",      \
           (cfg)->version, (cfg)->flags, (cfg)->queue_serialno_offs, \
           (cfg)->queue_label_offs);
#else
# define trace_workq_kernreturn(op, arg2, arg3, arg4)
# define trace_pthread_workqueue_setup(cfg)
#endif

#define WORKQ_CB_ARGS    (pthread_priority_t arg1)
#define KEVENT_CB_ARGS   (void **arg1, int *arg2)
#define WORKLOOP_CB_ARGS (uint64_t *arg1, void **arg2, int *arg3)

#define WORKQ_CB(name)    void (*name) WORKQ_CB_ARGS
#define KEVENT_CB(name)   void (*name) KEVENT_CB_ARGS
#define WORKLOOP_CB(name) void (*name) WORKLOOP_CB_ARGS

/*
 * Sources:
 *  libpthread/private/pthread/workqueue_private.h
 *  libpthread/kern/kern_internal.h
 *  libpthread/priority_private.h
 *  xnu/bsd/pthread/workqueue_syscalls.h
 */

/*
 * WORKQ_EXIT_THREAD_NKEVENT
 *  value for nkevents passed to _pthread_wqthread () by the kernel
 *  when requesting for libpthread to have a workqueue thread exit.
 */
#define WORKQ_EXIT_THREAD_NKEVENT (-1)

#define WQ_FLAG_THREAD_REUSE 0x00020000

typedef unsigned long pthread_priority_t;

#define _PTHREAD_PRIORITY_OVERCOMMIT_FLAG    0x80000000u
#define _PTHREAD_PRIORITY_INHERIT_FLAG       0x40000000u
#define _PTHREAD_PRIORITY_ROOTQUEUE_FLAG     0x20000000u
#define _PTHREAD_PRIORITY_SCHED_PRI_FLAG     0x20000000u
#define _PTHREAD_PRIORITY_ENFORCE_FLAG       0x10000000u
#define _PTHREAD_PRIORITY_FALLBACK_FLAG      0x04000000u
#define _PTHREAD_PRIORITY_COOPERATIVE_FLAG   0x08000000u
#define _PTHREAD_PRIORITY_EVENT_MANAGER_FLAG 0x02000000u
#define _PTHREAD_PRIORITY_NEEDS_UNBIND_FLAG  0x01000000u
#define _PTHREAD_PRIORITY_DEFAULTQUEUE_FLAG  0x04000000u
#define _PTHREAD_PRIORITY_OVERRIDE_QOS_FLAG  0x00800000u

#define _PTHREAD_PRIORITY_QOS_CLASS_MASK          0x003fff00u
#define _PTHREAD_PRIORITY_QOS_CLASS_SHIFT         (8ull)
#define _PTHREAD_PRIORITY_VALID_QOS_CLASS_MASK    0x00003f00u
#define _PTHREAD_PRIORITY_VALID_OVERRIDE_QOS_MASK 0x003fc000u
#define _PTHREAD_PRIORITY_QOS_OVERRIDE_SHIFT      (14ull)

#define _PTHREAD_PRIORITY_FLAGS_MASK     0xff000000u
#define _PTHREAD_PRIORITY_SCHED_PRI_MASK 0x0000ffffu
#define _PTHREAD_PRIORITY_THREAD_TYPE_MASK \
  (_PTHREAD_PRIORITY_OVERCOMMIT_FLAG | _PTHREAD_PRIORTITY_COOPERATIVE_FLAG)

/*
 * workq opcode for __workq_kernreturn()
 *  __workq_kernreturn(int op, ...)
 */
#define WQOPS_THREAD_RETURN              0x004
#define WQOPS_QUEUE_NEWSPISUPP           0x010
#define WQOPS_QUEUE_REQTHREADS           0x020
#define WQOPS_QUEUE_REQTHREADS2          0x030
#define WQOPS_THREAD_KEVENT_RETURN       0x040
#define WQOPS_SET_EVENT_MANAGER_PRIORITY 0x080
#define WQOPS_THREAD_WORKLOOP_RETURN     0x100
#define WQOPS_SHOULD_NARROW              0x200
#define WQOPS_SETUP_DISPATCH             0x400

/*
 * struct pthread_workqueue_config:
 *  Workqueue config for pthread_workqueue_setup()
 */
#define PTHREAD_WORKQUEUE_CONFIG_VERSION               2
#define PTHREAD_WORKQUEUE_CONFIG_MIN_SUPPORTED_VERSION 1
#define PTHREAD_WORKQUEUE_CONFIG_SUPPORTED_FLAGS       0
struct pthread_workqueue_config
{
  uint32_t flags;
  uint32_t version;
  void (*kevent_cb) (void **events, int *nevents);
  void (*workloop_cb) (uint64_t *id, void **events, int *nevents);
  void (*workq_cb) (ulong arg);
  uint64_t queue_serialno_offs;
  uint64_t queue_label_offs;
};

/*
 * struct workq_dispatch_config:
 *  Workqueue config for __workq_kernreturn(WQOPS_SETUP_DISPATCH, ...)
 */
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

extern pthread_priority_t
_pthread_qos_class_encode (qos_class_t qos, int relpri, unsigned long flags);
extern qos_class_t _pthread_qos_class_decode (pthread_priority_t pp,
                                              int *relrpi,
                                              unsigned long *flags);

/* Workqueue system calls */
extern int __workq_open (void);
extern int __workq_kernreturn (int op, void *arg2, int arg3, int arg4);
extern int __kqueue_workloop_ctl (uintptr_t cmd, uint64_t options, void *addr,
                                  size_t size);

extern int __pthread_workqueue_setkill (int enable);
extern void _pthread_wqthread (pthread_t thread, mach_port_t kport,
                               void *stackbottom, void *kevents, int flags,
                               int nkevents);

extern int pthread_workqueue_setup (struct pthread_workqueue_config *cfg,
                                    size_t cfg_size);
extern int _pthread_workqueue_init (WORKQ_CB (), int, int);
extern int _pthread_workqueue_init_with_kevent (WORKQ_CB (), KEVENT_CB (), int,
                                                int);
extern int _pthread_workqueue_init_with_workloop (WORKQ_CB (), KEVENT_CB (),
                                                  WORKLOOP_CB (), int, int);
extern int pthread_workqueue_setdispatch_np (void (*) (int, int, void *));

#endif /* WQ_WRAPPERS_H */
