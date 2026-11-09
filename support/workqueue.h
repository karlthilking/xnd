/* workqueue.h */
#ifndef SUPPORT_WORKQ_H
#define SUPPORT_WORKQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>

#include "workqueue-defs.h"
#include "kevent-defs.h"

__BEGIN_DECLS

extern int workq_setup (struct pthread_workqueue_config *);
extern int workq_setup_workloop (void (*) (pthread_priority_t),
                                 void (*) (void **, int *),
                                 void (*) (uint64_t *, void **, int *));
extern int workq_setup_kevent (void (*) (pthread_priority_t),
                               void (*) (void **, int *));
extern int workq_setup_worker (void (*) (pthread_priority_t));
extern int workq_reqthreads (int, pthread_priority_t, bool);
extern int workq_set_event_mgr_pri (pthread_priority_t);
extern int workq_kevent_submit (const struct kevent_qos_s *, int);

__END_DECLS

#endif /* SUPPORT_WORKQ_H */
