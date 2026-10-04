/* signal_wrappers.h */
#ifndef SIGNAL_WRAPPERS_H
#define SIGNAL_WRAPPERS_H

#include "xnd/xnd.h"
#include "xnd/platform/signal.h"

#include <signal.h>
#include <ucontext.h>

typedef void (*sig_t) (int);

void sig_state_save (void);
void sig_state_restore (void);

sig_t signal_hook (int, sig_t);
int sigaction_hook (int, const struct sigaction *, struct sigaction *);
int sigprocmask_hook (int, const sigset_t *, sigset_t *);
int pthread_sigmask_hook (int, const sigset_t *, sigset_t *);

#endif // SIGNAL_WRAPPERS_H
