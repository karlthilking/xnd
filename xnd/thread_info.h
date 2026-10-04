/* thread_info.h */
#ifndef XND_THREAD_INFO_H
#define XND_THREAD_INFO_H

#include <mach/mach.h>
#include <ucontext.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <signal.h>
#include <sys/queue.h>

#include "xnd.h"
#include "xnd_lib.h"
#include "platform/signal.h"
#include "common/time.h"

extern void pthread_yield_np(void);
extern int __bsdthread_ctl(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
extern int __pthread_workqueue_setkill(int);
extern int _pthread_workqueue_allow_send_signals(int);

enum thread_state {
	TS_EMBRYO,	/* uninitialized thread */
	TS_RUNNABLE,	/* mostly initialized, but synchronizing with
			   creator/parent thread */
	TS_RUNNING,	/* initialized and running */
	TS_ZOMBIE,	/* was exiting, possibly a zombie now */
	TS_RECLAIMED,	/* joined or cancelled, all resources for
			   this thread can be freed */
	TS_ATFORK,	/* running atfork handlers */
	TS_SIGNALED,	/* signaled by checkpoint thread */
	TS_SUSPENDING,	/* saving state in signal handler
			   before suspension */
	TS_SUSPENDED,	/* fully suspended in signal handler */
	TS_UNSAFE,	/* shouldn't be interrupted by a checkpoint
			   until done with current critical section */
};

struct thread_info
{
  /* Thread identity */
  pthread_t ti_self;
  mach_port_t ti_kport;
  uint64_t ti_tid;

  void *(*ti_start)(void *);
  void *ti_arg;

  /* Current execution context/state */
  enum thread_state ti_state;
  uint16_t ti_depth;
  uint16_t ti_refcnt;
  uint8_t ti_wq_thread : 1,
    ti_wq_free : 1,
    ti_ckpt_thread : 1,
    __unused : 5;

  /*
   * pthread_join context, ti_joinable is accessed atomically,
   * ti_joiner is protected by ti_lock.
   */
  bool ti_joinable;
  struct thread_info *ti_joiner;

  /* Saved user context */
  ucontext_t ti_uctx;

  /* Thread signal state */
  stack_t ti_sigstk;
  sigset_t ti_sigmask; /* Masked signals */
  sigset_t ti_siglist; /* Pending signals */

  /* TLS related or TSD-relative fields */
  uintptr_t ti_tsdbase;
  struct __darwin_pthread_handler_rec *ti_cleanup;

  pthread_mutex_t ti_lock;
  pthread_cond_t ti_cond;
  TAILQ_ENTRY (thread_info) ti_entry;
};

TAILQ_HEAD(thread_list, thread_info);

void thread_list_init(void);
void thread_list_destroy(void);
void thread_list_acquire(void);
void thread_list_release(void);
void thread_list_add_self(struct thread_info *);

void thread_list_atfork_prepare(void);
void thread_list_atfork_child(void);
void thread_list_atfork_parent(void);
void thread_list_atfork_failed(void);

void *thread_start (void *) __noreturn;
void *thread_restart (void *) __noreturn;
void thread_reap (struct thread_info *);
void thread_exit (void *) __noreturn;
void thread_sighandler (int, siginfo_t *, void *);
void thread_suspend_safe (void);

struct thread_info *thread_init(void *(*)(void *), void *);
struct thread_info *thread_self(void);
struct thread_info *thread_from_pthread_acquire_ref(pthread_t);

const char *const thread_state_string(enum thread_state) __cold;

/*
 * unsafe_enter_internal
 *  Try performing cmpxchg from TS_RUNNING to TS_UNSAFE in order to
 *  prevent a checkpoint during a critical section.
 *
 *  unsafe_enter_internal returns true if the cmpxchg succeeded before
 *  the next checkpoint. If the cmpxchg did not succeed until after
 *  restarting or resuming after a checkpoint, false is returned.
 */
static inline bool
unsafe_enter_internal (struct thread_info *t)
{
  bool ok, ret = true;
  enum thread_state state;
  uint64_t epoch = READ_ONCE (xnd_epoch);

  state = xnd_atomic_load (&t->ti_state, relaxed);
  if ((t->ti_depth++ == 0) && (state != TS_EMBRYO))
    {
      xnd_assert (state == TS_RUNNING || state == TS_SIGNALED);
      do
	{
	  if (state == TS_SIGNALED)
	    pthread_yield_np ();
	  state = TS_RUNNING;
	}
      while (!xnd_atomic_cmpxchgv_weak_acquire (&t->ti_state, &state,
					TS_UNSAFE));
    }

  return (READ_ONCE (xnd_epoch) == epoch);
}

static inline void
unsafe_exit_internal (struct thread_info *t)
{
  bool ok;
  enum thread_state state;

  state = xnd_atomic_load(&t->ti_state, relaxed);
  if ((--t->ti_depth == 0) && (state != TS_EMBRYO)) {
    ok = xnd_atomic_cmpxchg_release(&t->ti_state, TS_UNSAFE, TS_RUNNING);
    if (__xnd_unlikely(!ok))
      xnd_panic("unexpected thread state: %s\n",
		thread_state_string(t->ti_state));
  }
}

#define __unsafe_enter(t)				\
  ({							\
    bool __r = true;						\
    struct thread_info *__t = (t);			\
    xnd_compiler_barrier_acquire ();			\
    if (__t != NULL && __t->ti_ckpt_thread == 0)	\
      __r = unsafe_enter_internal (__t);		\
    xnd_compiler_barrier_release ();			\
    __r;						\
  })

#define __unsafe_exit(t)				\
  do							\
    {							\
      struct thread_info *__t = (t);			\
      xnd_compiler_barrier_acquire ();			\
      if (__t != NULL && __t->ti_ckpt_thread == 0)	\
	unsafe_exit_internal (__t);			\
      xnd_compiler_barrier_release ();			\
    }							\
  while (0)

#define unsafe_enter() 	__unsafe_enter (thread_self ())
#define unsafe_exit() 	__unsafe_exit (thread_self ())

#endif /* XND_THREAD_INFO_H  */
