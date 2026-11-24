/* 08-dispatch.c */
#include <dispatch/dispatch.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "common/types.h"
#include "common/xalloc.h"

#define TEST "08-dispatch"

static void random_fill (double *, size_t, double, double);
static double dot (double *, double *, size_t);
static double apply (dispatch_queue_t, size_t);
static dispatch_queue_t select_dispatch_queue (const char *, const char **);

static void
random_fill (double *v, size_t n, double lo, double hi)
{
  for (size_t i = 0; i < n; i++)
    v[i] = (drand48 () * (hi - lo)) + lo;
}

static double
dot (double *v, double *u, size_t n)
{
  double r = 0;

  for (size_t i = 0; i < n; i++)
    r += v[i] * u[i];

  return r;
}

static double
apply (dispatch_queue_t dq, size_t nthreads)
{
  double *results, global = 0;
  void (^block) (size_t) = NULL;
  const size_t n = (1 << 15) / nthreads;

  results = xmalloc (nthreads * sizeof (*results));
  block = ^(size_t idx)
    {
      double *x, *y, local = 0;
      const int rounds = 1 << 14;

      for (int k = rounds; k > 0; k--)
        {
          x = xmalloc (n * sizeof (*x));
          y = xmalloc (n * sizeof (*y));

          random_fill (x, n, -5.0, 5.0);
          random_fill (y, n, -5.0, 5.0);

          local += dot (x, y, n);
          free (x);
          free (y);
        }

      results[idx] = local;
    };

  dispatch_apply (nthreads, dq, block);
  for (size_t idx = 0; idx < nthreads; idx++)
    global += results[idx];

  free (results);
  return fabs (global / nthreads);
}

static dispatch_queue_t
select_dispatch_queue (const char *arg, const char **str)
{
  long priority;

  if (strcmp (arg, "high") == 0)
    {
      priority = DISPATCH_QUEUE_PRIORITY_HIGH;
      *str = "high";
    }
  else if (strcmp (arg, "low") == 0)
    {
      priority = DISPATCH_QUEUE_PRIORITY_LOW;
      *str = "low";
    }
  else if (strcmp (arg, "background") == 0)
    {
      priority = DISPATCH_QUEUE_PRIORITY_BACKGROUND;
      *str = "background";
    }
  else
    {
      priority = DISPATCH_QUEUE_PRIORITY_DEFAULT;
      *str = "default";
    }

  return dispatch_get_global_queue (priority, 0);
}

int
main (int argc, char *argv[])
{
  int iterations;
  size_t nthreads;
  dispatch_queue_t dq;
  double result, total = 0;
  const char *priority = NULL;

  iterations = argc > 1 ? atoi (argv[1]) : 50;
  nthreads = argc > 2 ? strtoull (argv[2], NULL, 10) : 8;

  if (argc > 3)
    dq = select_dispatch_queue (argv[3], &priority);
  else
    {
      dq = dispatch_get_global_queue (DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
      priority = "default";
    }

  srand48 (time (NULL));
  printf ("(%s) iterations: %d, threads: %zu, priority: %s\n",
          TEST, iterations, nthreads, priority);

  for (int i = 0; i < iterations; i++)
    {
      result = apply (dq, nthreads), total += result;
      printf ("Result %d:\t%f\n", i, result);
    }

  printf ("Total:\t%f\n", total);
  exit (0);
}
