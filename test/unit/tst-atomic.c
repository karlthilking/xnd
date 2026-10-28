/* tst-atomic.c */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "tst-support.h"
#include "common/atomic.h"
#include "common/compiler.h"

#define TEST_NAME "tst-atomic"

struct thread_args
{
  uint64_t steps;
  void (^callback) (void *);
  void *arg;
};

static int test_atomic_cmpxchg_spin (void);
static int do_test (void);

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
test_atomic_cmpxchg_spin (void)
{
  __block int lock;
  __block uint64_t count;
  uint64_t expected;
  struct thread_args args;

#if __has_feature (thread_sanitizer)
  args.steps = 5000;
#else
  args.steps = 100000;
#endif

  args.arg = NULL;
  args.callback = ^(void *arg)
    {
      xnd_atomic_cmpxchg_spin_acquire (&lock, 0, 1);
      count++;
      xnd_atomic_store_release (&lock, 0);
    };

  for (int n = 2; n <= 16; n <<= 1)
    {
      lock = 0, count = 0;
      expected = args.steps * n;
      spawn_threads (n, NULL, thread_doit, (void *) &args);
      if (count != expected)
        return 1;
    }

  return 0;
}

static int
do_test (void)
{
  int ret;
  
  if (test_atomic_cmpxchg_spin () != 0)
    {
      ret = 1;
      puts ("test failed: test_atomic_cmpxchg_spin");
    }
  else
    {
      ret = 0;
      puts ("test passed: test_atomic_cmpxchg_spin");
    }

  return ret;
}

#define TEST_FUNCTION do_test
#include "tst-support.c"
