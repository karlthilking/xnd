/* tst-xnd-ulock.c */
#include <fcntl.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "tst-support.h"
#include "synch/xnd-ulock.h"

#define TEST_NAME "tst-xnd-ulock"

static bool run_tests = true;
static bool run_benchmarks = false;

struct node
{
  int val;
  struct node *next;
};

struct thread_args
{
  uint64_t steps;
  void (^callback) (void *);
  void *arg;
};

static void *
thread_doit (void *arg)
{
  uint64_t step;
  struct thread_args *args = (struct thread_args *) arg;

  for (step = 0; step < args->steps; step++)
    args->callback (args->arg);

  pthread_exit (NULL);
}

static int
xnd_ulock_verify (void)
{
  __block uint64_t counter = 0;
  struct xnd_ulock ulock = XND_ULOCK_INITIALIZER;
  struct thread_args args;

  args.steps = 100000;
  args.arg = (void *) &ulock;
  args.callback = ^(void *lock) {
    struct xnd_ulock *ul = (struct xnd_ulock *) lock;
    xnd_ulock_lock (ul);
    counter++;
    xnd_ulock_unlock (ul);
  };

  for (int n = 2; n <= 32; n <<= 1)
    {
      spawn_threads (n, NULL, thread_doit, (void *) &args);
      if ((n * args.steps) != counter)
        return 1;
      counter = 0;
    }

  __block struct node *head = NULL;
  __block uint64_t insertions = 0, deletions = 0;

  args.steps = 2500;
  args.callback = ^(void *lock) {
    struct node *node = NULL;
    struct xnd_ulock *ul = (struct xnd_ulock *) lock;
    for (int i = 0; i < 5; i++)
      {
        node = xmalloc (sizeof (*node));
        xnd_ulock_lock (ul);
        node->next = head;
        head = node;
        insertions++;
        xnd_ulock_unlock (ul);
      }
    for (int i = 0; i < 5; i++)
      {
        xnd_ulock_lock (ul);
        node = head;
        head = head->next;
        deletions++;
        xnd_ulock_unlock (ul);
        free (node);
      }
  };

  for (int n = 2; n <= 32; n <<= 1)
    {
      spawn_threads (n, NULL, thread_doit, (void *) &args);
      if ((head != NULL) || (insertions != (5 * args.steps * n))
          || (insertions != deletions))
        return 1;
      insertions = 0, deletions = 0;
    }

  return 0;
}

static uint32_t
do_bench (int nthrds, uint64_t steps, void (^cb) (void *), void *arg)
{
  uint64_t start, end;
  struct thread_args args = {
    .steps = steps,
    .callback = cb,
    .arg = arg,
  };

  start = clock_gettime_nsec_np (CLOCK_UPTIME_RAW);
  spawn_threads (nthrds, NULL, thread_doit, (void *) &args);
  end = clock_gettime_nsec_np (CLOCK_UPTIME_RAW);

  return (uint32_t) ((end - start) / NSEC_PER_MSEC);
}

static uint32_t
xnd_ulock_bench (int nthrds, uint64_t steps, void (^work) (void))
{
  static struct xnd_ulock ulock = XND_ULOCK_INITIALIZER;
  void (^callback) (void *) = NULL;

  callback = ^(void *arg) {
    struct xnd_ulock *ul = (struct xnd_ulock *) arg;
    xnd_ulock_lock (ul);
    work ();
    xnd_ulock_unlock (ul);
  };

  return do_bench (nthrds, steps, callback, (void *) &ulock);
}

static uint32_t
pthread_mutex_bench (int nthrds, uint64_t steps, void (^work) (void))
{
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  void (^callback) (void *) = NULL;

  callback = ^(void *arg) {
    pthread_mutex_t *m = (pthread_mutex_t *) arg;
    xpthread_mutex_lock (m);
    work ();
    xpthread_mutex_unlock (m);
  };

  return do_bench (nthrds, steps, callback, (void *) &mutex);
}

static unsigned long
os_unfair_lock_bench (int nthrds, uint64_t steps, void (^work) (void))
{
  static os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
  void (^callback) (void *) = NULL;

  callback = ^(void *arg) {
    os_unfair_lock_t l = (os_unfair_lock_t) arg;
    os_unfair_lock_lock (l);
    work ();
    os_unfair_lock_unlock (l);
  };

  return do_bench (nthrds, steps, callback, (void *) &lock);
}

static int
do_test (void)
{
  int ret;
  struct subtest test = SUBTEST_DECL (xnd_ulock_verify);
  void (^run_bench) (uint64_t, void (^) (void)) = NULL;

  if (run_tests)
    ret = SUBTEST_CHECK (test);
  if (!run_benchmarks)
    return ret;

  run_bench = ^(uint64_t steps, void (^work) (void)) {
      uint32_t elapsed;
      uint32_t xnd_times[5], pthread_times[5], os_times[5];
      float xnd_avg = 0.0f, pthread_avg = 0.0f, os_avg = 0.0f;

      for (int n = 2; n <= 32; n <<= 1)
        {
          elapsed = xnd_ulock_bench (n, steps / n, work);
          printf ("xnd_ulock: %u ms (%d threads)\n", elapsed, n);
          xnd_times[__builtin_popcount (n - 1) - 1] = elapsed;

          elapsed = pthread_mutex_bench (n, steps / n, work);
          printf ("pthread_mutex: %u ms (%d threads)\n", elapsed, n);
          pthread_times[__builtin_popcount (n - 1) - 1] = elapsed;

          elapsed = os_unfair_lock_bench (n, steps / n, work);
          printf ("os_unfair_lock: %u ms (%d threads)\n", elapsed, n);
          os_times[__builtin_popcount (n - 1) - 1] = elapsed;

          putchar ('\n');
        }

      for (int i = 0; i < 5; i++)
        {
          xnd_avg += (float) *(xnd_times + i);
          pthread_avg += (float) *(pthread_times + i);
          os_avg += (float) *(os_times + i);
        }

      xnd_avg /= 5, pthread_avg /= 5, os_avg /= 5;
      printf ("xnd_ulock average: %f ms\n", xnd_avg);
      printf ("pthread_mutex average: %f ms\n", pthread_avg);
      printf ("os_unfair_lock average: %f ms\n", os_avg);
      putchar ('\n');
    };

  /* run benchmarks with fast critical section */
  {
    float *x, *y;
    const uint64_t steps = 20000000;
    void (^work) (void) = NULL;

    x = xmalloc (sizeof (*x) * 3);
    y = xmalloc (sizeof (*y) * 3);

    work = ^(void)
      {
        __unused float f = 0.0f;
        f += *(x + 0) * *(y + 0);
        f += *(x + 1) * *(y + 1);
        f += *(x + 2) * *(y + 2);
      };

    puts ("benchmarks (short critical section):");
    run_bench (steps, work);

    free (x);
    free (y);
  }

  /* run benchmarks with medium length critical section */
  {
    char *key, *txt;
    const uint64_t steps = 200000;
    void (^work) (void) = NULL;

    key = xmalloc (64);
    txt = xmalloc (64);

    work = ^(void)
      {
        for (int i = 63; i >= 0; i--)
          {
            key[i] = '0' + ((rand () & 1) ^ 1);
            txt[i] = '0' + ((rand () ^ 1) & 1);
          }
        txt[63] = '\0';
        setkey (key);
        for (int i = 5; i >= 0; i--)
          {
            encrypt (txt, 0);
            encrypt (txt, 1);
          }
      };

    puts ("benchmarks (medium critical section):");
    run_bench (steps, work);

    free (key);
    free (txt);
  }

  /* run benchmarks with long critical section */
  {
    const uint64_t steps = 250;
    void (^work) (void) = NULL;

    work = ^(void)
      {
        int fd;
        char *a, *b;
        const size_t n = 1 << 14;

        a = xmalloc (n);
        b = xmalloc (n);
        fd = open ("/dev/urandom", O_RDONLY);

        for (int i = 50; i >= 0; i--)
          {
            read (fd, a, n);
            read (fd, b, n);
            for (int k = n - 1; k >= 0; k--)
              {
                a[k] ^= b[n - 1 - k];
                b[k] ^= a[n - 1 - k];
              }
          }
        free (a);
        free (b);
        close (fd);
      };

    puts ("benchmarks (long critical section):");
    run_bench (steps, work);
  }

  return ret;
}

static void
prepare_test (int argc, char *argv[])
{
  char *opt = NULL;

#define shift *(argc--, argv++)
  shift;
  while (argc > 0)
    {
      if (strncmp (*argv, "--test=", const_strlen ("--test")) == 0)
        {
          opt = shift;
          if (strsep (&opt, "="), opt != NULL)
            run_tests = (atoi (opt) != 0);
        }
      else if (strncmp (*argv, "--bench=", const_strlen ("--bench")) == 0)
        {
          opt = shift;
          if (strsep (&opt, "="), opt != NULL)
            run_benchmarks = (atoi (opt) != 0);
        }
      else
        printf ("unrecognized option: %s\n", shift);
    }
}

#define PREPARE_FUNCTION prepare_test
#define TEST_FUNCTION do_test
#include "tst-support.c"
