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
  struct thread_args *args = (struct thread_args *)arg;

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
  args.arg = (void *)&ulock;
  args.callback = ^(void *lock) {
    struct xnd_ulock *ul = (struct xnd_ulock *)lock;
    xnd_ulock_lock (ul);
    counter++;
    xnd_ulock_unlock (ul);
  };

  for (int n = 2; n <= 32; n <<= 1)
    {
      spawn_threads (n, NULL, thread_doit, (void *)&args);
      if ((n * args.steps) != counter)
        return 1;
      counter = 0;
    }

  __block struct node *head = NULL;
  __block uint64_t insertions = 0, deletions = 0;

  args.steps = 2500;
  args.callback = ^(void *lock) {
    struct node *node = NULL;
    struct xnd_ulock *ul = (struct xnd_ulock *)lock;
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
      spawn_threads (n, NULL, thread_doit, (void *)&args);
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
  spawn_threads (nthrds, NULL, thread_doit, (void *)&args);
  end = clock_gettime_nsec_np (CLOCK_UPTIME_RAW);

  return (uint32_t)((end - start) / NSEC_PER_MSEC);
}

static uint32_t
xnd_ulock_bench (int nthrds, uint64_t steps, void (^work) (void))
{
  static struct xnd_ulock ulock = XND_ULOCK_INITIALIZER;
  void (^callback) (void *) = NULL;

  callback = ^(void *arg) {
    struct xnd_ulock *ul = (struct xnd_ulock *)arg;
    xnd_ulock_lock (ul);
    work ();
    xnd_ulock_unlock (ul);
  };

  return do_bench (nthrds, steps, callback, (void *)&ulock);
}

static uint32_t
pthread_mutex_bench (int nthrds, uint64_t steps, void (^work) (void))
{
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  void (^callback) (void *) = NULL;

  callback = ^(void *arg) {
    pthread_mutex_t *m = (pthread_mutex_t *)arg;
    xpthread_mutex_lock (m);
    work ();
    xpthread_mutex_unlock (m);
  };

  return do_bench (nthrds, steps, callback, (void *)&mutex);
}

static unsigned long
os_unfair_lock_bench (int nthrds, uint64_t steps, void (^work) (void))
{
  static os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
  void (^callback) (void *) = NULL;

  callback = ^(void *arg) {
    os_unfair_lock_t l = (os_unfair_lock_t)arg;
    os_unfair_lock_lock (l);
    work ();
    os_unfair_lock_unlock (l);
  };

  return do_bench (nthrds, steps, callback, (void *)&lock);
}

static int
do_test (void)
{
  uint32_t elapsed;
  uint64_t steps;
  void (^work) (void) = NULL;

  if (xnd_ulock_verify () != 0)
    {
      puts ("test failed: xnd_ulock_verify\n");
      return 1;
    }
  else
    puts ("test passed: xnd_ulock_verify\n");

  float *x, *y;
  x = xmalloc (sizeof (float) * 3);
  y = xmalloc (sizeof (float) * 3);

  work = ^(void) {
    __unused float f = 0.0f;
    f += *(x + 0) * *(y + 0);
    f += *(x + 1) * *(y + 1);
    f += *(x + 2) * *(y + 2);
  };

  steps = 25000000;
  puts ("benchmarks (short critical section):\n");
  for (int n = 2; n <= 32; n <<= 1)
    {
      elapsed = xnd_ulock_bench (n, steps / n, work);
      printf ("xnd_ulock: %u ms (%d threads)\n", elapsed, n);
      elapsed = pthread_mutex_bench (n, steps / n, work);
      printf ("pthread_mutex: %u ms (%d threads)\n", elapsed, n);
      elapsed = os_unfair_lock_bench (n, steps / n, work);
      printf ("os_unfair_lock: %u ms (%d threads)\n", elapsed, n);
      putchar ('\n');
    }

  free (x);
  free (y);

  char *key, *txt;
  key = xmalloc (64);
  txt = xmalloc (64);

  work = ^(void) {
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

  steps = 200000;
  puts ("benchmarks (medium critical section):\n");
  for (int n = 2; n <= 32; n <<= 1)
    {
      elapsed = xnd_ulock_bench (n, steps / n, work);
      printf ("xnd_ulock: %u ms (%d threads)\n", elapsed, n);
      elapsed = pthread_mutex_bench (n, steps / n, work);
      printf ("pthread_mutex: %u ms (%d threads)\n", elapsed, n);
      elapsed = os_unfair_lock_bench (n, steps / n, work);
      printf ("os_unfair_lock: %u ms (%d threads)\n", elapsed, n);
      putchar ('\n');
    }

  free (key);
  free (txt);

  work = ^(void) {
    int fd;
    char *a, *b;
    const size_t n = 1 << 14;

    a = xmalloc (n), b = xmalloc (n);
    fd = open ("/dev/urandom", O_RDONLY);
    for (int i = 50; i >= 0; i--)
      {
        read (fd, a, n);
        read (fd, b, n);
        for (int k = n; k >= 0; k--)
          {
            a[k] ^= b[n - k];
            b[k] ^= a[n - k];
          }
      }
    free (a);
    free (b);
    close (fd);
  };

  steps = 250;
  puts ("benchmarks (long critical section):\n");
  for (int n = 2; n <= 32; n <<= 1)
    {
      elapsed = xnd_ulock_bench (n, steps / n, work);
      printf ("xnd_ulock: %u ms (%d threads)\n", elapsed, n);
      elapsed = pthread_mutex_bench (n, steps / n, work);
      printf ("pthread_mutex: %u ms (%d threads)\n", elapsed, n);
      elapsed = os_unfair_lock_bench (n, steps / n, work);
      printf ("os_unfair_lock: %u ms (%d threads)\n", elapsed, n);
      putchar ('\n');
    }

  return 0;
}

#define TEST_FUNCTION do_test
#include "tst-support.c"
