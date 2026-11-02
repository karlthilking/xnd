/* wq_wrappers.h */
#ifndef WQ_WRAPPERS_H
#define WQ_WRAPPERS_H

#include "xnd/xnd.h"
#include "xnd/workqueue-defs.h"
#include <pthread.h>
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

extern pthread_priority_t _pthread_qos_class_encode (qos_class_t qos,
                                                     int relpri,
                                                     unsigned long flags);
extern qos_class_t _pthread_qos_class_decode (pthread_priority_t pp,
                                              int *relrpi,
                                              unsigned long *flags);

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
