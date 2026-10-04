/* pthread_wrappers.c */
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include "xnd/xnd.h"
#include "common/time.h"
#include "xnd/util/env.h"
#include "xnd/thread_info.h"
#include "xnd/tls.h"
#include "xnd/interpose.h"
#include "time_wrappers.h"
#include "pthread_wrappers.h"

int
pthread_create_hook (pthread_t *p, const pthread_attr_t *attr,
                     void *(*start_routine) (void *), void *arg)
{
  int err, detach, cancelstate;
  struct thread_info *t = NULL;

  unsafe_enter ();
  t = thread_init (start_routine, arg);
  if (t == NULL)
    {
      err = EAGAIN;
      goto out;
    }

  if (attr != NULL)
    {
      err = pthread_attr_getdetachstate (attr, &detach);
      if (err != 0)
        {
          err = EINVAL;
          goto out;
        }
      t->ti_joinable = (detach == PTHREAD_CREATE_JOINABLE);
    }

  xpthread_mutex_lock (&t->ti_lock);
  err = pthread_create (p, attr, thread_start, t);
  if (err != 0)
    {
      xpthread_mutex_unlock (&t->ti_lock);
      goto out;
    }

  /*
	 * pthread_create() is not a cancellation point, but our hook
	 * can be cancelled in pthread_cond_wait(). Temporarily
	 * disable cancellation to ensure we are not cancelled while
	 * synchronizing with the child thread.
	 */
  pthread_setcancelstate (PTHREAD_CANCEL_DISABLE, &cancelstate);
  while (t->ti_state == TS_EMBRYO)
    xpthread_cond_wait (&t->ti_cond, &t->ti_lock);

  xpthread_mutex_unlock (&t->ti_lock);
  if (cancelstate != PTHREAD_CANCEL_DISABLE)
    pthread_setcancelstate (cancelstate, NULL);

out:
  if (err != 0 && t != NULL)
    thread_reap (t);

  unsafe_exit ();
  return err;
}

/*
 * pthread_joiner_cond_cleanup:
 *  Cleanup function that runs if the joiner in pthread_join_hook() was
 *  cancelled in pthread_cond_wait. Clear t->ti_joiner, release the
 *  thread lock, and drop our reference to the thread (t->ti_refcnt).
 */
static void
pthread_joiner_cond_cleanup (void *arg)
{
  struct thread_info *t, *self;

  /*
	 * Cleanup functions are called before tsd destructors execute,
	 * so thread_self() should not return NULL.
	 */
  self = thread_self ();
  xnd_assert (self != NULL);

  t = (struct thread_info *)arg;
  xnd_assert (t->ti_joiner == self);

  t->ti_joiner = NULL;
  xpthread_mutex_unlock (&t->ti_lock);

  xnd_atomic_dec_release (&t->ti_refcnt);
}

static void
pthread_joiner_cleanup (void *arg)
{
  struct thread_info *t = (struct thread_info *)arg;

  /*
	 * We called unsafe_enter() before pthread_join(), and are
	 * also still holding a reference to the target thread.
	 * Call unsafe_exit() and drop our reference to reverse our
	 * join state.
	 */
  unsafe_exit ();
  xnd_atomic_dec_release (&t->ti_refcnt);
}

static inline bool
pthread_join_invalid (struct thread_info *t)
{
  bool ret, joinable;

  /*
	 * Thread is not joinable (detached with pthread_detach(), is
	 * a workqueue thread, etc.), or already has a joiner.
	 */
  xpthread_mutex_lock (&t->ti_lock);
  joinable = xnd_atomic_load (&t->ti_joinable, acquire);
  ret = (!joinable || t->ti_joiner != NULL);
  xpthread_mutex_unlock (&t->ti_lock);

  return ret;
}

static inline bool
pthread_join_deadlock (struct thread_info *t, struct thread_info *joiner)
{
  bool ret;

  xpthread_mutex_lock (&joiner->ti_lock);
  ret = (joiner->ti_joiner == t);
  xpthread_mutex_unlock (&joiner->ti_lock);

  return ret;
}

int
pthread_join_hook (pthread_t p, void **value_ptr)
{
  int err;
  struct thread_info *t, *self;
  enum thread_state state;

  self = thread_self ();
  xnd_assert (self != NULL);

  t = thread_from_pthread_acquire_ref (p);
  state = xnd_atomic_load (&t->ti_state, relaxed);

  if (t == NULL || state == TS_RECLAIMED)
    {
      err = ESRCH;
      goto out;
    }

  if (t == self)
    {
      err = EDEADLK;
      goto out;
    }

  /*
	 * Checks if the thread is not joinable, or if the thread
	 * already has an active joiner.
	 */
  if (pthread_join_invalid (t))
    {
      err = EINVAL;
      goto out;
    }

  /*
	 * Checks if the target thread and the calling thread are
	 * attempting to join each other.
	 */
  if (pthread_join_deadlock (t, self))
    {
      err = EDEADLK;
      goto out;
    }

  xpthread_mutex_lock (&t->ti_lock);
  t->ti_joiner = self;
  while (t->ti_state != TS_ZOMBIE)
    {
      /*
		 * pthread_joiner_cond_cleanup will clear t->ti_joiner,
		 * release t->ti_lock, and drop our reference to the
		 * thread if we are cancelled in pthread_cond_wait.
		 */
      pthread_cleanup_push (pthread_joiner_cond_cleanup, t);
      xpthread_cond_wait (&t->ti_cond, &t->ti_lock);
      pthread_cleanup_pop (0);

      /*
		 * If t->ti_joinable changes from true to false while
		 * we were waiting, the thread was detached via
		 * pthread_detach().
		 */
      if (!t->ti_joinable)
        {
          t->ti_joiner = NULL;
          xpthread_mutex_unlock (&t->ti_lock);
          err = EINVAL;
          goto out;
        }
    }
  t->ti_joiner = NULL;
  xpthread_mutex_unlock (&t->ti_lock);

  unsafe_enter ();
  pthread_cleanup_push (pthread_joiner_cleanup, t);
  err = pthread_join (t->ti_self, value_ptr);
  pthread_cleanup_pop (0);
  unsafe_exit ();

  if (err == 0)
    xnd_atomic_store (&t->ti_state, TS_RECLAIMED, relaxed);

out:
  xnd_atomic_dec_release (&t->ti_refcnt);
  return err;
}

int
pthread_detach_hook (pthread_t p)
{
  int err;
  bool wake = false;
  struct thread_info *t;

  t = thread_from_pthread_acquire_ref (p);
  if (t == NULL)
    return ESRCH;

  unsafe_enter ();
  err = pthread_detach (p);
  if (err == 0)
    {
      wake = true;
      xnd_atomic_store (&t->ti_joinable, false, release);
    }
  unsafe_exit ();

  if (wake)
    {
      xpthread_mutex_lock (&t->ti_lock);
      if (t->ti_joiner != NULL)
        xpthread_cond_signal (&t->ti_cond);
      xpthread_mutex_unlock (&t->ti_lock);
    }

  xnd_atomic_dec_release (&t->ti_refcnt);
  return err;
}

int
pthread_kill_hook (pthread_t p, int sig)
{
  int err, ckptsig = env_get_ckpt_signal ();

  if (sig == ckptsig)
    {
      xnd_warn ("signal is reserved: %s\n", strsignal (ckptsig));
      return EINVAL;
    }

  unsafe_enter ();
  err = pthread_kill (p, sig);
  unsafe_exit ();

  return err;
}

INTERPOSE (pthread_create_hook, pthread_create);
INTERPOSE (pthread_join_hook, pthread_join);
INTERPOSE (pthread_detach_hook, pthread_detach);
INTERPOSE (pthread_kill_hook, pthread_kill);
