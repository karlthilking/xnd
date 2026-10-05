/* pthread_wrappers.h */
#ifndef PTHREAD_WRAPPERS_H
#define PTHREAD_WRAPPERS_H

#include <mach/mach.h>
#include <pthread.h>

extern int pthread_main_np (void);
extern pthread_t pthread_main_thread_np (void);
extern void *pthread_get_stackaddr_np (pthread_t);
extern size_t pthread_get_stacksize_np (pthread_t);
extern void pthread_yield_np (void);
extern int pthread_cond_signal_thread_np (pthread_cond_t *, pthread_t);

extern int __pthread_kill (mach_port_t, int);
extern int __pthread_sigmask (int, const sigset_t *, sigset_t *);
extern int __pthread_workqueue_setkill (int);
extern u64 __thread_selfid (void);
extern int __disable_threadsignal (int);

int pthread_create_hook (pthread_t *, const pthread_attr_t *,
                         void *(*) (void *), void *);
int pthread_join_hook (pthread_t, void **);
int pthread_detach_hook (pthread_t);
int pthread_kill_hook (pthread_t, int);

#endif /* PTHREAD_WRAPPERS_H */
