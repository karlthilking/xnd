/* tst-support.h */
#ifndef XND_TEST_H
#define XND_TEST_H

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "common/compiler.h"
#include "common/atomic.h"
#include "common/time.h"
#include "common/xthread.h"
#include "common/xalloc.h"

struct test_config
{
  const char *test_name;
  void (*prepare_function) (int argc, char **argv);
  int (*test_function) (void);
  int (*test_function_argv) (int argc, char **argv);
  void (*cleanup_function) (void);
};

struct subtest
{
  int status;
  const char *name;
  int (*callback) (void);
};

#define SUBTEST_DECL(func) \
  { 0, STRINGIFY (func), func }

#define SUBTEST_CHECK(t)                                \
  ({                                                    \
    struct subtest *__t = (t);                          \
    __t->status = (*__t->callback) ();                  \
    const char *__fmt =                                 \
      (__t->status == 0                                 \
       ? "test passed: %s\n" : "test failed: %s\n");    \
    printf (__fmt, __t->name);                          \
    __t->status;                                        \
  })

int subprocess_run (const char *, char *const *);
void spawn_threads (int, const pthread_attr_t *, void *(*) (void *), void *);

#endif /* XND_TEST_H */
