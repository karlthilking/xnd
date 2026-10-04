/* signal.h */
#ifndef XND_SIGNAL_H
#define XND_SIGNAL_H

#include "xnd/xnd.h"

#include <signal.h>
#include <ucontext.h>

#define UC_TRAD 1
#define UC_FLAVOR 30

#define SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP 0x0400

#define SIGTERMSET (sigmask(SIGINT) | sigmask(SIGTERM) | sigmask(SIGQUIT))
#define SIGCANTSET (sigmask(SIGKILL) | sigmask(SIGSTOP))

#define sigisemptyset(set) (*(set) == (sigset_t)0)

#define valid_signal(sig)			\
	({					\
		int __sig = (sig);		\
		__sig > 0 && sig < NSIG;	\
	})

static inline void
sigandset(sigset_t *set, const sigset_t *left, const sigset_t *right)
{
	int sig;

	for (sig = 1; sig < NSIG; sig++) {
		if (sigismember(left, sig) && sigismember(right, sig))
			sigaddset(set, sig);
	}
}

static inline void
sigorset(sigset_t *set, const sigset_t *left, const sigset_t *right)
{
	int sig;

	for (sig = 1; sig < NSIG; sig++) {
		if (sigismember(left, sig) || sigismember(right, sig))
			sigaddset(set, sig);
	}
}

int xnd_sigaction(int, const struct sigaction *, struct sigaction *);

#endif /* XND_SIGNAL_H */
