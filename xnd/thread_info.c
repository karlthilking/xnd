/* thread_info.c */
#include <errno.h>
#include <mach/mig.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <malloc/malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include "xnd.h"
#include "thread_info.h"
#include "xnd_lib.h"
#include "pac.h"
#include "tls.h"
#include "workq.h"
#include "common/time.h"
#include "util/env.h"
#include "util/log.h"
#include "coordinator/xnd_coord_api.h"
#include "coordinator/xnd_coord_client.h"
#include "wrappers/signal_wrappers.h"
#include "wrappers/pthread_wrappers.h"
#include "platform/ucontext/ucontext.h"

static inline void thread_list_add_unlocked (void);
static void thread_list_add (void);
static void thread_list_remove (struct thread_info *);

static bool thread_reapable (struct thread_info *);
static void thread_fini (void *);
static void thread_barrier (void);
static void thread_save_tls (void);
static void thread_restore_tls (struct thread_info *);
static void thread_restore_context (void) __noreturn;
static void thread_save_sig_state (ucontext_t *);
static void thread_restore_sig_state (void);

static void ckpt_thread_init (void);
static void ckpt_thread_exit (void) __noreturn;
static void ckpt_thread_wait (void);
static void ckpt_thread_reap (void);
static void *ckpt_thread_work (void *);

static void zombie_list_init (void);
static void zombie_list_destroy (void);
static void zombie_list_filter (void);
static void zombie_list_add (struct thread_info *);
static void zombie_list_remove (struct thread_info *);
static inline void zombie_list_acquire (void);
static inline void zombie_list_release (void);

static void barrier_arrival_wait (void);
static void barrier_release (void);
static void raise_pending_signals (void);
static inline bool try_suspend_threads (int, int *);
static void suspend_threads (void);
static void restore_threads (void);
static void wait_for_exiting_threads (void);
static void prewake_joiner_threads (void);

static _Thread_local struct thread_info *myself = NULL;
static struct thread_info *_main_thread = NULL;
static struct thread_info ckpt_thread = { 0 };

/*
 * _xnd_is_threaded serves the same purpose as pthread_is_threaded_np(),
 * but excludes the checkpoint thread.
 */
__private_extern bool _xnd_is_threaded = false;

__private_extern struct thread_list thread_list;
static pthread_mutex_t thread_list_lock = PTHREAD_MUTEX_INITIALIZER;

static struct thread_list zombie_list;
static pthread_mutex_t zombie_list_lock = PTHREAD_MUTEX_INITIALIZER;

static sigset_t p_siglist;

__private_extern pthread_key_t thread_self_key = 0;
__private_extern pthread_key_t tlv_flag_key = 0;

static volatile int barrier_seq = 0;
static volatile int barrier_expected;
static volatile int barrier_arrived;

static pthread_cond_t cond_arrived = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cond_released = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t ckpt_mtx = PTHREAD_MUTEX_INITIALIZER;

void
thread_list_init (void)
{
  int sig, err;

  TAILQ_INIT (&thread_list);

  xpthread_key_create (&tlv_flag_key, NULL);
  xnd_tlv_init ();

  _main_thread = thread_init (NULL, NULL);
  if (_main_thread == NULL)
    xnd_panic ("failed to allocate thread descriptor\n");

  myself = _main_thread;
  myself->ti_self = pthread_self ();
  myself->ti_kport = mach_thread_self ();
  myself->ti_tid = __thread_selfid ();
  myself->ti_state = TS_RUNNING;

  xpthread_key_create (&thread_self_key, thread_fini);
  xpthread_setspecific (thread_self_key, myself);

  thread_list_add_unlocked ();

  /*
	 * Call unsafe_enter() to mirror the call to unsafe_exit()
	 * before __fork_hook returns. The main thread's state becomes
	 * TS_ATFORK to inform the checkpoint thread not to proceed
	 * while atfork handlers are still running.
	 */
  if (get_xnd_state () == XND_ATFORK)
    {
      unsafe_enter ();
      myself->ti_state = TS_ATFORK;
    }

  zombie_list_init ();
  ckpt_thread_init ();
}

void
thread_list_destroy (void)
{
  struct thread_info *t, *next;

  xnd_assert (get_xnd_state () == XND_EXITING);
  if (myself != &ckpt_thread)
    ckpt_thread_reap ();

  TAILQ_FOREACH_SAFE (t, &thread_list, ti_entry, next)
    thread_reap (t);

  zombie_list_destroy ();
  xnd_tlv_fini ();
}

void
thread_list_acquire (void)
{
  xpthread_mutex_lock (&thread_list_lock);
}

void
thread_list_release (void)
{
  xpthread_mutex_unlock (&thread_list_lock);
}

static inline void
thread_list_add_unlocked (void)
{
  xnd_assert (myself != NULL);
  TAILQ_INSERT_HEAD (&thread_list, myself, ti_entry);
}

static void
thread_list_add (void)
{
  thread_list_acquire ();
  thread_list_add_unlocked ();
  thread_list_release ();
}

void
thread_list_add_self (struct thread_info *t)
{
  xnd_tlv_init ();
  myself = t;
  thread_list_add ();
}

/*
 * thread_list_remove:
 *  Remove a thread from the thread list. thread_list_remove should
 *  only ever be called by the checkpoint thread, and should be
 *  called while thread_list_lock is already acquired. Joined threads
 *  are freed, while other exited threads are inserted into a zombie
 *  list until they are joined.
 */
static void
thread_list_remove (struct thread_info *t)
{
  TAILQ_REMOVE (&thread_list, t, ti_entry);
  if (__xnd_unlikely (t->ti_ckpt_thread))
    return;

  if (thread_reapable (t))
    {
      thread_reap (t);
      return;
    }

  zombie_list_add (t);
}

/*
 * thread_list_atfork_prepare:
 *  Acquire all mutexes in the parent before fork(). All locks that
 *  will be used in the child are acquired and will later be
 *  re-initialized in the child process's atfork handler.
 */
void
thread_list_atfork_prepare (void)
{
  struct thread_info *t;

  thread_list_acquire ();
  zombie_list_acquire ();
  xpthread_mutex_lock (&ckpt_mtx);
  xpthread_mutex_lock (&ckpt_thread.ti_lock);

  TAILQ_FOREACH (t, &thread_list, ti_entry)
    xpthread_mutex_lock (&t->ti_lock);
  TAILQ_FOREACH (t, &zombie_list, ti_entry)
    xpthread_mutex_lock (&t->ti_lock);
}

/*
 * thread_list_atfork_child:
 *  Reset thread list to only include the main thread in the child
 *  process (caller of thread_list_atfork_child).
 *
 *  All locks acquired in thread_list_atfork_prepare are unlocked in
 *  reverse order. Thread descriptors from the parent process are
 *  destroyed. Locks and condition variables are re-initialized, and
 *  a new checkpoint thread is spawned.
 */
void
thread_list_atfork_child (void)
{
  struct thread_info *t, *next;

  /*
	 * Release all thread descriptor locks, and free each
	 * thread descriptor and any associated resources.
	 */
  TAILQ_FOREACH_SAFE (t, &thread_list, ti_entry, next)
    {
      xpthread_mutex_unlock (&t->ti_lock);
      thread_reap (t);
    }
  TAILQ_FOREACH_SAFE (t, &zombie_list, ti_entry, next)
    {
      xpthread_mutex_unlock (&t->ti_lock);
      thread_reap (t);
    }

  xpthread_mutex_unlock (&ckpt_thread.ti_lock);
  xpthread_mutex_unlock (&ckpt_mtx);
  zombie_list_release ();
  thread_list_release ();

  /*
	 * Initialize thread list and main thread struct, zombie list,
	 * and spawn a new checkpoint thread. Delete tsd keys before
	 * they are re-initialized in thread_list_init.
	 */
  xpthread_setspecific (tlv_flag_key, NULL);
  xpthread_setspecific (thread_self_key, NULL);
  xpthread_key_delete (tlv_flag_key);
  xpthread_key_delete (thread_self_key);
  myself = NULL;
  thread_list_init ();

  /*
	 * Re-initialize all statically-initialized mutexes and
	 * condition variables.
	 */
  xpthread_mutex_init (&ckpt_mtx, NULL);
  xpthread_mutex_init (&thread_list_lock, NULL);
  xpthread_mutex_init (&zombie_list_lock, NULL);
  xpthread_cond_init (&cond_arrived, NULL);
  xpthread_cond_init (&cond_released, NULL);

  /*
	 * Change thread state from TS_ATFORK to TS_RUNNING in order
	 * to allow the checkpoint thread to resume execution.
	 */
  xpthread_mutex_lock (&ckpt_thread.ti_lock);
  myself->ti_state = TS_RUNNING;
  xpthread_cond_signal (&ckpt_thread.ti_cond);
  xpthread_mutex_unlock (&ckpt_thread.ti_lock);
}

/*
 * thread_list_atfork_parent:
 *  Release all locks that were acquired in thread_list_atfork_prepare.
 */
void
thread_list_atfork_parent (void)
{
  struct thread_info *t;

  TAILQ_FOREACH (t, &zombie_list, ti_entry)
    xpthread_mutex_unlock (&t->ti_lock);
  TAILQ_FOREACH (t, &thread_list, ti_entry)
    xpthread_mutex_unlock (&t->ti_lock);

  xpthread_mutex_unlock (&ckpt_thread.ti_lock);
  xpthread_mutex_unlock (&ckpt_mtx);
  zombie_list_release ();
  thread_list_release ();
}

void
thread_list_atfork_failed (void)
{
  struct thread_info *t;

  TAILQ_FOREACH (t, &zombie_list, ti_entry)
    xpthread_mutex_unlock (&t->ti_lock);
  TAILQ_FOREACH (t, &thread_list, ti_entry)
    xpthread_mutex_unlock (&t->ti_lock);

  xpthread_mutex_unlock (&ckpt_thread.ti_lock);
  xpthread_mutex_unlock (&ckpt_mtx);
  zombie_list_release ();
  thread_list_release ();
}

static void
zombie_list_init (void)
{
  TAILQ_INIT (&zombie_list);
}

static void
zombie_list_destroy (void)
{
  struct thread_info *t, *next;

  TAILQ_FOREACH_SAFE (t, &zombie_list, ti_entry, next)
    thread_reap (t);
}

static inline void
zombie_list_acquire (void)
{
  xpthread_mutex_lock (&zombie_list_lock);
}

static inline void
zombie_list_release (void)
{
  xpthread_mutex_unlock (&zombie_list_lock);
}

static void
zombie_list_filter (void)
{
  struct thread_info *t, *next;

  zombie_list_acquire ();
  TAILQ_FOREACH_SAFE (t, &zombie_list, ti_entry, next)
    {
      if (thread_reapable (t))
        zombie_list_remove (t);
    }
  zombie_list_release ();
}

static void
zombie_list_add (struct thread_info *t)
{
  zombie_list_acquire ();
  TAILQ_INSERT_HEAD (&zombie_list, t, ti_entry);
  zombie_list_release ();
}

/*
 * zombie_list_remove:
 *  If a exited thread's thread descriptor isn't needed anymore, the
 *  zombie thread can be removed the zombie list and its resources can
 *  be freed. zombie_list_remove should only be called directly by
 *  zombie_list_filter (to scan for thread desciptors that are no longer
 *  needed), and the lock should be held by the caller.
 */
static void
zombie_list_remove (struct thread_info *t)
{
  TAILQ_REMOVE (&zombie_list, t, ti_entry);
  thread_reap (t);
}

/*
 * thread_init:
 *  Initialize new thread with start routine and argument that were
 *  included as arguments to pthread_create.
 *
 *  thread_init should be called by the pthread_create wrapper to
 *  initialize a new thread, and the thread should added to the thread
 *  list by calling thread_list_add in the thread start routine
 *  wrapper/trampoline function.
 */
struct thread_info *
thread_init (void *(*start_routine) (void *), void *arg)
{
  int err;
  struct thread_info *t = NULL;

  /*
	 * Either the caller should be the thread initializing itself
	 * (main thread or a workqueue thread), and thus, myself == NULL,
	 * or the caller is a parent thread in __pthread_create_hook.
	 * If the caller is a parent thread, the parent must be in
	 * state TS_UNSAFE to prevent a checkpoint signal from
	 * interrupting thread_init.
	 */
  xnd_assert (myself == NULL || myself->ti_state == TS_UNSAFE);

  t = calloc (1, sizeof (*t));
  if (t == NULL)
    return NULL;

  t->ti_start = start_routine;
  t->ti_arg = arg;
  t->ti_state = TS_EMBRYO;
  t->ti_joinable = true;

  err = pthread_mutex_init (&t->ti_lock, NULL);
  if (err != 0)
    {
      free (t);
      return NULL;
    }

  err = pthread_cond_init (&t->ti_cond, NULL);
  if (err != 0)
    {
      pthread_mutex_destroy (&t->ti_lock);
      free (t);
      return NULL;
    }

  return t;
}

static bool
thread_reapable (struct thread_info *t)
{
  u16 ref;
  bool ret, joinable, has_joiner;
  enum thread_state state;

  ref = xnd_atomic_load (&t->ti_refcnt, acquire);
  state = xnd_atomic_load (&t->ti_state, relaxed);

  switch (state)
    {
    case TS_ZOMBIE:
      joinable = xnd_atomic_load (&t->ti_joinable, acquire);
      if (!joinable)
        {
          /*
			 * If we can't grab t->ti_lock, assume that
			 * the joiner is holding the lock.
			 */
          if (pthread_mutex_trylock (&t->ti_lock) != 0)
            {
              has_joiner = true;
            }
          else
            {
              has_joiner = (t->ti_joiner != NULL);
              pthread_mutex_unlock (&t->ti_lock);
            }
        }
      ret = !(joinable || has_joiner || ref > 0);
      break;
    case TS_RECLAIMED:
      ret = (ref == 0);
      break;
    default:
      ret = false;
      break;
    }

  /*
	 * Reclaimed threads without a reference can be freed.
	 * Zombie threads without a reference can be freed iff
	 * they are not joinable (detached or workqueue thread).
	 */
  return ret;
}

/*
 * thread_reap:
 *  Free all resources associated with this thread. thread_reap should
 *  be called once a thread has exited and been joined by another thread.
 */
void
thread_reap (struct thread_info *t)
{
  xpthread_mutex_destroy (&t->ti_lock);
  xpthread_cond_destroy (&t->ti_cond);
  free (t);
}

/*
 * thread_fini:
 *  Destructor registered with pthread_key_create, sets ti_state to
 *  TS_ZOMBIE and signals a waiter in pthread_join_hook if one exists.
 */
static void
thread_fini (void *arg)
{
  bool ok;
  struct thread_info *self = (struct thread_info *) arg;

  do
    {
      ok = xnd_atomic_cmpxchg_weak_acquire (&self->ti_state, TS_RUNNING,
                                            TS_ZOMBIE);
    }
  while (!ok);

  xpthread_mutex_lock (&self->ti_lock);
  if (self->ti_joiner != NULL)
    xpthread_cond_signal (&self->ti_cond);
  xpthread_mutex_unlock (&self->ti_lock);

  xnd_tlv_fini ();
}

struct thread_info *
thread_self (void)
{
  return myself;
}

/*
 * thread_list_find_pthread:
 *  Find thread struct from pthread in the specified thread list
 *  (either thread_list or zombie_list). The lock for the associated
 *  thread list should be held.
 */
static inline struct thread_info *
thread_list_find_pthread (struct thread_list *list, pthread_t p)
{
  struct thread_info *t;

  TAILQ_FOREACH (t, list, ti_entry)
    {
      if (t->ti_self == p)
        return t;
    }

  return NULL;
}

/*
 * thread_from_pthread_acquire_ref:
 *  Finds the thread struct associated with a given pthread, and adds
 *  a reference to the thread while holding a list lock to prevent the
 *  checkpoint thread from deallocating the thread.
 *
 *  The list lock is acquired and released within an unsafe_(enter|exit)
 *  pair so we are not checkpointed while holding the list lock.
 */
struct thread_info *
thread_from_pthread_acquire_ref (pthread_t p)
{
  struct thread_info *t, *thread = NULL;
  pthread_mutex_t *list_lock = NULL;

  unsafe_enter ();
  thread_list_acquire ();
  t = thread_list_find_pthread (&thread_list, p);
  if (t != NULL)
    {
      thread = t;
      list_lock = &thread_list_lock;
      goto out;
    }
  thread_list_release ();

  zombie_list_acquire ();
  t = thread_list_find_pthread (&zombie_list, p);
  if (t != NULL)
    {
      thread = t;
      list_lock = &zombie_list_lock;
      goto out;
    }
  zombie_list_release ();

out:
  if (thread != NULL)
    {
      xnd_atomic_inc_acquire (&thread->ti_refcnt);
      xpthread_mutex_unlock (list_lock);
    }

  unsafe_exit ();
  return thread;
}

static void
ckpt_thread_init (void)
{
  struct thread_info *t = &ckpt_thread;

  t->ti_ckpt_thread = 1;
  t->ti_state = TS_EMBRYO;

  xpthread_mutex_init (&t->ti_lock, NULL);
  xpthread_cond_init (&t->ti_cond, NULL);

  xpthread_mutex_lock (&t->ti_lock);
  xpthread_create (&t->ti_self, NULL, ckpt_thread_work, NULL);

  while (t->ti_state == TS_EMBRYO)
    xpthread_cond_wait (&t->ti_cond, &t->ti_lock);

  xpthread_mutex_unlock (&t->ti_lock);
}

static void
ckpt_thread_exit (void)
{
  xnd_assert (myself == &ckpt_thread);

  xpthread_mutex_lock (&myself->ti_lock);
  myself->ti_state = TS_ZOMBIE;
  xpthread_cond_signal (&myself->ti_cond);
  xpthread_mutex_unlock (&myself->ti_lock);

  xnd_tlv_fini ();
  pthread_exit (NULL);
  unreachable ();
}

static void
ckpt_thread_wait (void)
{
  int ret;
  bool exited, do_exit;
  sigset_t set;

  ret = wait_for_ckpt_request_from_coord (&exited);
  if (ret != 0)
    {
      do_exit = (exited || get_xnd_state () == XND_EXITING);
      if (do_exit)
        {
          ckpt_thread_exit ();
          unreachable ();
        }
      xnd_panic ("failed to receive checkpoint request\n");
    }

  /*
	 * In case a user thread happened to call sigprocmask(),
	 * refill the checkpoint thread's signal mask before we
	 * attempt to checkpoint.
	 */
  sigfillset (&set);
  xpthread_sigmask (SIG_SETMASK, &set, NULL);
}

static void *
ckpt_thread_work (void *arg)
{
  sigset_t set;
  static volatile bool restart;

  /* Block all signals so user threads can handle caught signals
     rather than interrupting the checkpoint thread. */
  sigfillset (&set);
  xpthread_sigmask (SIG_SETMASK, &set, NULL);

  xnd_tlv_init ();
  myself = &ckpt_thread;
  myself->ti_self = pthread_self ();
  myself->ti_kport = mach_thread_self ();
  myself->ti_tid = __thread_selfid ();

  /* Signal to main thread that initialization is finished and
     the checkpoint thread is ready to proceed. */
  xpthread_mutex_lock (&myself->ti_lock);
  myself->ti_state = TS_RUNNING;
  xpthread_cond_signal (&myself->ti_cond);

  /*
   * If this is a child process handling atfork routines, park
   * here until the main thread finishes executing atfork
   * handlers.
   */
  while (_main_thread->ti_state == TS_ATFORK)
    xpthread_cond_wait (&myself->ti_cond, &myself->ti_lock);

  xpthread_mutex_unlock (&myself->ti_lock);

  restart = false;
  getcontext (&myself->ti_uctx);

  if (restart)
    {
      set_xnd_state (XND_RESTARTING);
      xnd_postrestart_early ();

      thread_restore_tls (&ckpt_thread);
      xnd_tlv_init ();

#if DEBUG || DEVELOPMENT
      if (myself != &ckpt_thread)
        {
          xnd_warn ("myself != &ckpt_thread\n");
          myself = &ckpt_thread;
        }
      if (myself->ti_self != pthread_self ())
        {
          xnd_warn ("myself->ti_self != pthread_self()\n");
          myself->ti_self = pthread_self ();
        }
#else
      myself = &ckpt_thread;
      myself->ti_self = pthread_self ();
#endif
      myself->ti_kport = mach_thread_self ();

      xnd_postrestart_late ();
      restore_threads ();
      workq_restore ();
      barrier_arrival_wait ();

      thread_restore_sig_state ();
      zombie_list_filter ();
      raise_pending_signals ();
      prewake_joiner_threads ();

      set_xnd_state (XND_RUNNING);
      workq_resume ();
      barrier_release ();
    }

  restart = true;
  for (;;)
    {
      xnd_log_ckpt_thread_info (myself);
      /*
     * Wait until coordinator sends XND_CKPT_REQUEST, and
     * transition from XND_RUNNING to XND_CKPT_PENDING.
     *
     * Now that a checkpoint is pending, enter a global
     * coordinator barrier until the coordinator
     * responds with XND_CKPT_START.
     */
      ckpt_thread_wait ();
      set_xnd_state (XND_CKPT_PENDING);
      enter_coord_barrier (COORD_BARRIER_PRECKPT);

      thread_save_tls ();
      thread_save_sig_state (NULL);

      /*
     * Suspend user threads and transition from XND_CKPTPENDING
     * to XND_SUSPENDING.
     */
      set_xnd_state (XND_SUSPINPROG);
      workq_suspend ();
      suspend_threads ();
      wait_for_exiting_threads ();
      zombie_list_filter ();

      /*
     * Wait for all threads to arrive at the barrier and
     * transition from XND_SUSPENDING -> XND_CKPTINPROG.
     */
      barrier_arrival_wait ();

      set_xnd_state (XND_CKPTINPROG);
      xnd_precheckpoint ();

      xnd_tlv_fini ();
      xnd_checkpoint (&myself->ti_uctx);
      xnd_tlv_init ();

      /*
     * Checkpoint is complete, now wait in another coordinator
     * barrier while the coordinator writes the checkpoint
     * manifest.
     */
      enter_coord_barrier (COORD_BARRIER_POSTCKPT);
      xnd_postcheckpoint ();

      /*
     * Release user threads
     *  XND_CKPTINPROG -> XND_RUNNING
     */
      set_xnd_state (XND_RUNNING);
      workq_resume ();
      barrier_release ();
    }

  pthread_exit (NULL);
}

static void
ckpt_thread_reap (void)
{
  int err;
  struct thread_info *t = &ckpt_thread;

  xnd_assert (myself != NULL && myself != &ckpt_thread);

  xpthread_mutex_lock (&t->ti_lock);
  while (t->ti_state != TS_ZOMBIE)
    xpthread_cond_wait (&t->ti_cond, &t->ti_lock);
  xpthread_mutex_unlock (&t->ti_lock);

  err = pthread_join (t->ti_self, NULL);
  if (err != 0)
    xnd_strerror ("failed to join checkpoint thread", err);

  xpthread_mutex_destroy (&t->ti_lock);
  xpthread_cond_destroy (&t->ti_cond);
}

/*
 * barrier_arrival_wait:
 *  Wait for all user threads to reach thread_barrier.
 */
static void
barrier_arrival_wait (void)
{
  xpthread_mutex_lock (&ckpt_mtx);
  while (barrier_arrived < barrier_expected)
    xpthread_cond_wait (&cond_arrived, &ckpt_mtx);
  xpthread_mutex_unlock (&ckpt_mtx);
}

/*
 * barrier_release:
 *  Allow user threads to resume after checkpoint.
 */
static void
barrier_release (void)
{
  xpthread_mutex_lock (&ckpt_mtx);
  barrier_seq++;
  xpthread_cond_broadcast (&cond_released);
  xpthread_mutex_unlock (&ckpt_mtx);
}

/*
 * raise_pending_signals:
 *  Raise signals that were pending for all threads, assuming that
 *  this means the signal was orignally directed to the entire
 *  process rather than individual threads. raise_pending_signals()
 *  should be called only once all threads have restored their signal
 *  masks (in thread_restore_sig_state).
 */
void
raise_pending_signals (void)
{
  int sig, ckptsig = env_get_ckpt_signal ();
  pid_t pid = _real_getpid ();

  for (sig = 1; sig < NSIG; sig++)
    {
      if (sig == ckptsig)
        continue;
      if (sigismember (&p_siglist, sig))
        {
          if (kill (pid, sig) != 0)
            xnd_warn ("kill: %s\n", strerror (errno));
        }
    }
}

static inline bool
try_suspend_threads (int ckptsig, int *count)
{
  struct thread_info *t, *next;
  int err, sig, nsuspended = 0;
  bool hit, retry = false;
  enum thread_state state;

  thread_list_acquire ();
  TAILQ_FOREACH_SAFE (t, &thread_list, ti_entry, next)
    {
      if (__xnd_unlikely (t->ti_ckpt_thread))
        {
          xnd_warn ("checkpoint thread in thread list\n");
          thread_list_remove (t);
          continue;
        }

      sig = 0;
      state = xnd_atomic_load (&t->ti_state, acquire);
      switch (state)
        {
        case TS_RUNNING:
          sig = ckptsig;
          hit = xnd_atomic_cmpxchg_weak_acq_rel (&t->ti_state, state,
                                                 TS_SIGNALED);
          if (!hit)
            {
              retry = true;
              break;
            }
          fallthrough;
        case TS_SIGNALED:
          /*
			 * state = TS_RUNNING -> sig = ckptsig
			 * state = TS_SIGNALED -> sig = 0
			 */
          err = pthread_kill (t->ti_self, sig);
          if (err == ESRCH)
            {
              thread_list_remove (t);
              continue;
            }
          else if (err != 0)
            {
              xnd_strerror ("pthread_kill", err);
            }
          fallthrough;
        case TS_UNSAFE:
        case TS_EMBRYO:
        case TS_RUNNABLE:
          retry = true;
          break;
        case TS_SUSPENDED:
        case TS_SUSPENDING:
          nsuspended++;
          break;
        case TS_RECLAIMED:
          thread_list_remove (t);
          break;
        case TS_ZOMBIE:
          break;
        default:
          unreachable ();
        }
    }
  thread_list_release ();

  *count = nsuspended;
  return retry;
}

static void
suspend_threads (void)
{
  bool retry;
  int count, sig = env_get_ckpt_signal ();

  xpthread_mutex_lock (&ckpt_mtx);
  barrier_arrived = 0;

  do
    {
      retry = try_suspend_threads (sig, &count);
      if (retry)
        usleep (50);
    }
  while (retry);

  barrier_expected = count;
  xpthread_mutex_unlock (&ckpt_mtx);
}

static void
restore_threads (void)
{
  pthread_t p;
  pthread_attr_t attr, *attrp;
  sigset_t list, mask;
  struct thread_info *t;

  barrier_arrived = 0;
  barrier_expected = 0;

  xpthread_attr_init (&attr);
  xpthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

  xpthread_mutex_lock (&ckpt_mtx);
  thread_list_acquire ();

  sigemptyset (&list);
  mask = ckpt_thread.ti_siglist;

  TAILQ_FOREACH (t, &thread_list, ti_entry)
    {
      barrier_expected++;
      sigandset (&list, &mask, &t->ti_siglist), mask = list;
      /* workqueue threads are restored independently of normal user
	 threads via workq_restore () called in ckpt_thread_work () */
      if (t->ti_wq_thread)
        continue;
      attrp = (t->ti_joinable ? NULL : &attr);
      xpthread_create (&p, attrp, thread_restart, t);
    }

  /* p_siglist is the intersection of every thread's set of
     pending signals, informing us of which pending signals
     were process-directed rather than thread-directed */
  p_siglist = list;
  thread_list_release ();
  xpthread_mutex_unlock (&ckpt_mtx);
  xpthread_attr_destroy (&attr);
}

static void
wait_for_exiting_threads (void)
{
  int err, exiting, exited;
  struct thread_info *t, *next;
  enum thread_state state;

  thread_list_acquire ();
  do
    {
      exiting = 0;
      exited = 0;
      TAILQ_FOREACH_SAFE (t, &thread_list, ti_entry, next)
        {
          state = xnd_atomic_load (&t->ti_state, relaxed);
          switch (state)
            {
            case TS_RECLAIMED:
              thread_list_remove (t);
              break;
            case TS_ZOMBIE:
              exiting++;
              err = pthread_kill (t->ti_self, 0);
              if (err == ESRCH)
                {
                  exited++;
                  thread_list_remove (t);
                }
              break;
            default:
              break;
            }
        }

      usleep (50);
    }
  while (exiting != exited);
  thread_list_release ();
}

static void
prewake_joiner_threads (void)
{
  struct thread_info *t;
  pthread_t joiner;

  zombie_list_acquire ();
  TAILQ_FOREACH (t, &zombie_list, ti_entry)
    {
      xpthread_mutex_lock (&t->ti_lock);
      if (t->ti_joiner != NULL)
        {
          joiner = t->ti_joiner->ti_self;
          pthread_cond_signal_thread_np (&t->ti_cond, joiner);
        }
      xpthread_mutex_unlock (&t->ti_lock);
    }
  zombie_list_release ();
}

static void
thread_barrier (void)
{
  int seq;

  xpthread_mutex_lock (&ckpt_mtx);
  seq = barrier_seq;

  if (++barrier_arrived == barrier_expected)
    xpthread_cond_signal (&cond_arrived);

  while (barrier_seq == seq)
    xpthread_cond_wait (&cond_released, &ckpt_mtx);

  xpthread_mutex_unlock (&ckpt_mtx);
}

void
thread_sighandler (int sig, siginfo_t *info, void *uctx)
{
  bool ok;
  static _Thread_local int cancelstate;
  static _Thread_local bool is_restart;

  xnd_assert (myself != NULL);
  if (myself->ti_ckpt_thread)
    {
      xnd_warn ("checkpoint thread in %s\n", __func__);
      return;
    }

  ok = xnd_atomic_cmpxchg_acq_rel (&myself->ti_state, TS_SIGNALED,
                                   TS_SUSPENDING);
  if (__xnd_unlikely (!ok))
    xnd_panic ("unexpected thread state: %s\n",
               thread_state_string (myself->ti_state));

  pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, &cancelstate);

  /* Save state and transition to suspended */
  thread_save_tls ();
  thread_save_sig_state ((ucontext_t *) uctx);

  WRITE_ONCE (is_restart, false);
  getcontext (&myself->ti_uctx);
  if (READ_ONCE (is_restart))
    goto out;

  WRITE_ONCE (is_restart, true);
  xnd_tlv_fini ();

  xnd_atomic_store (&myself->ti_state, TS_SUSPENDED, seq_cst);

  /* Wait in barrier before resuming */
  thread_barrier ();
  xnd_tlv_init ();

out:
  if (cancelstate != PTHREAD_CANCEL_DISABLE)
    pthread_setcancelstate (cancelstate, NULL);

  xnd_atomic_store (&myself->ti_state, TS_RUNNING, release);
}

/*
 * thread_suspend_safe
 *  Safely suspend for a checkpoint voluntarily.
 *
 *  If unsafe_enter () succeeds before a checkpoint occurs, the
 *  calling thread can safely manipulate its signal mask atomically
 *  and suspend for a checkpoint using sigwait ().
 *
 *  If unsafe_enter () did not succeed until after a checkpoint
 *  occurred, the calling thread still achieved the effect of pausing
 *  its execution until a checkpoint. The thread can simply call
 *  unsafe_exit () before returning.
 */
void
thread_suspend_safe (void)
{
  sigset_t mask, omask;
  int ret, sig, ckptsig = env_get_ckpt_signal ();

  sigemptyset (&mask);
  sigaddset (&mask, ckptsig);

  if (unsafe_enter ())
    {
      xpthread_sigmask (SIG_BLOCK, &mask, &omask);
      unsafe_exit ();
    }
  else
    {
      unsafe_exit ();
      return;
    }

  ret = sigwait (&mask, &sig);
  if (ret != 0)
    xnd_panic ("sigwait: %s\n", strerror (errno));
  if (sig != ckptsig)
    xnd_panic ("(sig = %s) != ckptsig\n", strsignal (sig));

  xpthread_sigmask (SIG_SETMASK, &omask, NULL);
}

void *
thread_start (void *arg)
{
  void *exit_value;

  xnd_atomic_store (&_xnd_is_threaded, true, relaxed);

  xnd_tlv_init ();
  myself = (struct thread_info *) arg;

  myself->ti_self = pthread_self ();
  myself->ti_kport = mach_thread_self ();
  myself->ti_tid = __thread_selfid ();

  xpthread_setspecific (thread_self_key, myself);
  thread_list_add ();

  /*
	 * Wake up the parent thread in __pthread_create_hook, who
	 * may have start sleeping on the child's condition variable
	 * duirng initialization.
	 */
  xpthread_mutex_lock (&myself->ti_lock);
  xnd_atomic_store (&myself->ti_state, TS_RUNNABLE, release);
  xpthread_cond_signal (&myself->ti_cond);
  xpthread_mutex_unlock (&myself->ti_lock);

  /*
	 * Safe to be checkpointed now once finished synchronizing
	 * with parent thread.
	 */
  xnd_atomic_store (&myself->ti_state, TS_RUNNING, release);
  exit_value = (*myself->ti_start) (myself->ti_arg);

  pthread_exit (exit_value);
  unreachable ();
}

void *
thread_restart (void *thread)
{
  struct thread_info *t = (struct thread_info *) thread;

  /* Restore thread-local storage first */
  thread_restore_tls (t);
  xnd_tlv_init ();

#if DEBUG || DEVELOPMENT
  if (myself != t)
    {
      xnd_warn ("myself != t\n");
      myself = t;
    }
  if (myself->ti_self != pthread_self ())
    {
      xnd_warn ("myself->ti_self != pthread_self()\n");
      myself->ti_self = pthread_self ();
    }
#else
  myself = t;
  myself->ti_self = pthread_self ();
#endif

  myself->ti_kport = mach_thread_self ();

  thread_restore_sig_state ();
  thread_barrier ();
  thread_restore_context ();

  unreachable ();
}

static void
thread_save_tls (void)
{
  pthread_t self;

  /* Save tsd base address for restore */
  myself->ti_tsdbase = self_tsd_base ();

  /*
	 * pthread cleanup handlers can be pushed onto a thread's
	 * stack by external functions in thread_sighandler, and
	 * will be saved with the rest of user-space memory
	 * in a checkpoint. Save the head of pthread's cleanup stack
	 * here so the cleanup stack will not contain garbage stack
	 * memory after restart.
	 */
  self = pthread_self ();
  myself->ti_cleanup = self->__cleanup_stack;
}

static void
thread_restore_tls (struct thread_info *t)
{
  u64 *tidaddr;
  pthread_t self;

  _thread_set_tsd_base ((void *) t->ti_tsdbase);

  tidaddr = tsd_relative_access (u64, TSD_THREADID_OFFSET);
  *tidaddr = t->ti_tid;

  /* Restore head of cleanup stack (saved in thread_save_tls) */
  self = tsd_getspecific (__TSD_THREAD_SELF);
  self->__cleanup_stack = t->ti_cleanup;

  /* Refresh thread mach port */
  tsd_setspecific (__TSD_MACH_THREAD_SELF, mach_thread_self ());

  /*
	 * Zero out other thread-specific mach ports so that can be
	 * re-initialized lazily.
	 *
	 * mig_get_reply_port() will re-initialize the reply port
	 * with mach_port_construct() when tsd[__TSD_MIG_REPLY] is set
	 * to MACH_PORT_NULL.
	 *
	 * mig_get_special_reply_port will re-initialize the special
	 * reply port with thread_get_special_reply_port() when
	 * tsd[__TSD_MACH_SPECIAL_REPLY] is set to MACH_PORT_NULL.
	 *
	 * os_get_cached_semaphore() will create a new semaphore
	 * with _os_semaphore_create if the cached semaphore is
	 * set to SEMAPHORE_NULL.
	 *
	 * FIXME:
	 *  To be more precise, it would be necessary to place wrappers
	 *  around library functions that use any of these ports, so
	 *  they will not be invalidated if we checkpoint while the
	 *  function is executing.
	 */
  tsd_setspecific (__TSD_MIG_REPLY, MACH_PORT_NULL);
  tsd_setspecific (__TSD_MACH_SPECIAL_REPLY, MACH_PORT_NULL);
  tsd_setspecific (__TSD_SEMAPHORE_CACHE, SEMAPHORE_NULL);
}

/*
 * thread_restore_context:
 *  Re-sign return addresses on the stack to avoid pointer
 *  authentication failures once we restore the saved user context.
 *  Then, do a manual context switch back to this thread's savd
 *  context.
 */
static void
thread_restore_context (void)
{
  u64 *fp = (u64 *) get_ucontext_fp (&myself->ti_uctx);

  ptrauth_resign_frames (fp);
  xnd_setcontext (&myself->ti_uctx);

  unreachable ();
}

/*
 * thread_save_sig_state:
 *  Save thread's signal mask, alternate signal stack, and set
 *  of pending signals so this thread can recreate their signal
 *  state after restart.
 */
static void
thread_save_sig_state (ucontext_t *ucp)
{
  int ret;

  /*
	 * The checkpoint thread's signal mask should always be full.
	 * User threads should save ucp->uc_sigmask to obtain their
	 * signal mask before the checkpoint signal was received.
	 */
  if (myself->ti_ckpt_thread)
    {
      sigset_t fullmask;
      sigfillset (&fullmask);
      myself->ti_sigmask = fullmask;
    }
  else
    {
      myself->ti_sigmask = ucp->uc_sigmask;
    }

  ret = sigaltstack (NULL, &myself->ti_sigstk);
  if (ret != 0)
    {
      xnd_perror ("sigaltstack");
      myself->ti_sigstk.ss_flags = SS_DISABLE;
    }

  ret = sigpending (&myself->ti_siglist);
  if (ret != 0)
    {
      xnd_perror ("sigpending");
      sigemptyset (&myself->ti_siglist);
    }
}

/*
 * thread_restore_sig_state:
 *  Restore the calling thread's saved signal mask and alternate
 *  signal stack if one was registered. Once the signal mask is
 *  restored, we can re-raise thread-direct signals that were
 *  pending at checkpoint.
 */
static void
thread_restore_sig_state (void)
{
  int err, ret, ckptsig = env_get_ckpt_signal ();

  /*
	 * sigreturn will already restore this signal mask once
	 * the calling thread returns from our signal handler, but
	 * restoring the signal mask early will allow us to re-raise
	 * pending signals.
	 */
  err = pthread_sigmask (SIG_SETMASK, &myself->ti_sigmask, NULL);
  if (err != 0)
    {
      xnd_strerror ("pthread_sigmask", err);
      sigemptyset (&myself->ti_sigmask);
    }

  if ((myself->ti_sigstk.ss_flags & SS_DISABLE) == 0)
    {
      myself->ti_sigstk.ss_flags &= ~SS_ONSTACK;
      ret = sigaltstack (&myself->ti_sigstk, NULL);
      if (ret != 0)
        xnd_perror ("sigaltstack");
    }

  if (sigisemptyset (&myself->ti_siglist)
      || sigisemptyset (&myself->ti_sigmask))
    return;

  /*
	 * Re-raise thread-directed pending signals. Pending signals
	 * that were determined to be process-directed (p_siglist) are
	 * skipped.
	 */
  for (int sig = 1; sig < NSIG; sig++)
    {
      if (sig == ckptsig || sigismember (&p_siglist, sig))
        continue;
      if (sigismember (&myself->ti_siglist, sig)
          && sigismember (&myself->ti_sigmask, sig))
        {
          err = pthread_kill (myself->ti_self, sig);
          if (err != 0)
            xnd_strerror ("pthread_kill", err);
        }
    }
}

const char *const
thread_state_string (enum thread_state state)
{
  switch (state)
    {
    case TS_EMBRYO:
      return "TS_EMBRYO";
    case TS_RUNNABLE:
      return "TS_RUNNABLE";
    case TS_RUNNING:
      return "TS_RUNNING";
    case TS_ZOMBIE:
      return "TS_ZOMBIE";
    case TS_RECLAIMED:
      return "TS_RECLAIMED";
    case TS_ATFORK:
      return "TS_FORK";
    case TS_SIGNALED:
      return "TS_SIGNALED";
    case TS_SUSPENDING:
      return "TS_SUSPENDING";
    case TS_SUSPENDED:
      return "TS_SUSPENDED";
    case TS_UNSAFE:
      return "TS_UNSAFE";
    default:
      break;
    }

  return NULL;
}
