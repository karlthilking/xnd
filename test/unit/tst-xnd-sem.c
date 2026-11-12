/* tst-xnd-sem.c */
#include <pthread.h>

#include "common/atomic.h"
#include "common/compiler.h"
#include "common/xalloc.h"
#include "common/xthread.h"
#include "tst-support.h"
#include "synch/xnd-sem.h"

#define TEST_NAME "tst-xnd-sem"

struct thread_args
{
  uint64_t steps;
  void (^callback) (void);
};

static void *
thread_doit (void *arg)
{
  struct thread_args *args = (struct thread_args *) arg;

  for (uint64_t i = 0; i < args->steps; i++)
    args->callback ();

  pthread_exit (NULL);
}

static int
test_xnd_sem_mutex (void)
{
  int nthreads;
  struct thread_args args;
  __block struct xnd_sem sem;
  __block uint64_t count;

  xnd_sem_init (&sem, 1);
  args.steps = 2500;
  args.callback = ^(void)
    {
      xnd_sem_wait (&sem);
      count++;
      xnd_sem_post (&sem);
    };

  for (nthreads = 2; nthreads < 32; nthreads++)
    {
      count = 0;
      spawn_threads (nthreads, NULL, thread_doit, (void *) &args);
      T_CHECK ((args.steps * nthreads) == count);
    }

  return 0;
}

static int
test_xnd_sem_count (void)
{
  const int nthreads = 8;
  struct thread_args args;
  pthread_t *threads = NULL;
  __block struct xnd_sem sem_consumer;
  __block struct xnd_sem sem_producer;
  __block uint64_t produced, consumed;

  xnd_sem_init (&sem_producer, 0);
  xnd_sem_init (&sem_consumer, 0);
  args.steps = 250;
  args.callback = ^(void)
    {
      xnd_sem_wait (&sem_consumer);
      xnd_atomic_dec_relaxed (&produced);
      xnd_atomic_inc_release (&consumed);
      xnd_sem_post (&sem_producer);
    };

  threads = xmalloc (nthreads * sizeof (*threads));
  for (int i = 0; i < nthreads; i++)
    xpthread_create (&threads[i], NULL, thread_doit, (void *) &args);

  for (int i = 0; i < args.steps; i++)
    {
      produced = nthreads;
      consumed = 0;

      for (int j = 0; j < nthreads; j++)
        xnd_sem_post (&sem_consumer);
      for (int j = 0; j < nthreads; j++)
        xnd_sem_wait (&sem_producer);

      T_CHECK (xnd_atomic_load_acquire (&produced) == 0);
      T_CHECK (xnd_atomic_load_acquire (&consumed) == nthreads);
    }

  for (int i = 0; i < nthreads; i++)
    xpthread_join (threads[i], NULL);

  free (threads);
  return 0;
}

static int
test_xnd_sem_timeout (void)
{
  int err;
  struct xnd_sem sem;
  struct timespec start, end;
  uint64_t elapsed, timeout = 50 * NSEC_PER_MSEC;

  xnd_sem_init (&sem, 0);
  clock_gettime (CLOCK_UPTIME_RAW, &start);
  err = xnd_sem_timedwait (&sem, timeout);
  clock_gettime (CLOCK_UPTIME_RAW, &end);

  T_CHECK (err == ETIMEDOUT);
  elapsed = (end.tv_sec - start.tv_sec) * NSEC_PER_SEC;
  elapsed += end.tv_nsec - start.tv_nsec;
  T_CHECK (elapsed >= timeout);

  return 0;
}

static int
test_xnd_sem_wake_during_timedwait (void)
{
  int err;
  pthread_t thread;
  struct thread_args args;
  __block struct xnd_sem sem;
  uint64_t start, end, elapsed;
  __block uint64_t timeout = 50 * NSEC_PER_MSEC;

  xnd_sem_init (&sem, 0);
  args.steps = 1;
  args.callback = ^(void)
    {
      struct timespec ts;

      ts.tv_sec = (timeout / 2) / NSEC_PER_SEC;
      ts.tv_nsec = (timeout / 2) % NSEC_PER_SEC;
      
      if (nanosleep (&ts, NULL) != 0)
        {
          perror ("nanosleep");
          abort ();
        }
      
      xnd_sem_post (&sem);
    };

  start = clock_gettime_nsec_np (CLOCK_UPTIME_RAW);
  xpthread_create (&thread, NULL, thread_doit, (void *) &args);

  err = xnd_sem_timedwait (&sem, timeout);
  end = clock_gettime_nsec_np (CLOCK_UPTIME_RAW);
  T_CHECK (err == 0);

  elapsed = end - start;
  T_CHECK (elapsed >= timeout / 2);
  T_CHECK (elapsed < timeout);

  pthread_join (thread, NULL);
  return 0;
}

static int
do_test (void)
{
  int ret;
  struct subtest tests[] =
    {
      SUBTEST_DECL (test_xnd_sem_mutex),
      SUBTEST_DECL (test_xnd_sem_count),
      SUBTEST_DECL (test_xnd_sem_timeout),
      SUBTEST_DECL (test_xnd_sem_wake_during_timedwait),
    };

  for (int i = 0; i < countof (tests); i++)
    {
      ret = SUBTEST_CHECK (tests[i]);
      if (ret != 0)
        return ret;
    }

  return 0;
}

#define TEST_FUNCTION do_test
#include "tst-support.c"
