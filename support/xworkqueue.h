/* xworkqueue.h */
#ifndef SUPPORT_XWORKQ_H
#define SUPPORT_XWORKQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>

#include "workqueue-defs.h"
#include "kevent-defs.h"

__BEGIN_DECLS

extern void xworkq_setup (struct pthread_workqueue_config *);
extern void xworkq_setup_workloop (void (*) (pthread_priority_t),
                                   void (*) (void **, int *),
                                   void (*) (uint64_t *, void **, int *));
extern void xworkq_setup_kevent (void (*) (pthread_priority_t),
                                 void (*) (void **, int *));
extern void xworkq_setup_worker (void (*) (pthread_priority_t));
extern void xworkq_reqthreads (int, pthread_priority_t, bool);
extern void xworkq_set_event_mgr_pri (pthread_priority_t);
extern void xworkq_kevent_submit (const struct kevent_qos_s *, int);

__END_DECLS

#endif /* SUPPORT_XWORKQ_H */
