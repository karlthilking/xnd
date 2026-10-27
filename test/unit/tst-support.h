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

#define T_FAIL(fmt, ...)                                            \
  ({                                                                \
    dprintf (2, "(%s:%s:%d) ", __FILE_NAME__, __func__, __LINE__);  \
    dprintf (2, fmt, ##__VA_ARGS__);                                \
  })

#define T_FAIL_RETURN(ret, fmt, ...)            \
  ((void)                                       \
   ({                                           \
     T_FAIL (fmt, ##__VA_ARGS__);               \
     return (ret);                              \
   }))
#define T_FAIL_EXIT(exit_status, fmt, ...)      \
  ((void)                                       \
   ({                                           \
     T_FAIL (fmt, ##__VA_ARGS__);               \
     exit (exit_status);                        \
   }))
#define T_FAIL_ABORT(fmt, ...)                  \
  ((void)                                       \
   ({                                           \
     T_FAIL (fmt, ##__VA_ARGS__);               \
     abort ();                                  \
   }))

#define T_ASSERT(expr)                                            \
  (__xnd_unlikely (!(expr))                                       \
   ? T_FAIL_ABORT ("assertion failed: (%s)\n", STRINGIFY (expr))  \
   : ((void) 0))

#define T_CHECK(expr) \
  (__xnd_unlikely (!(expr)) \
   ? T_FAIL_RETURN (1, "check failed: (%s)\n", STRINGIFY (expr)) \
   : ((void) 0))

struct subtest
{
  const char *name;
  int (*callback) (void);
};

#define SUBTEST_DECL(func) \
  { STRINGIFY (func), func }

#define SUBTEST_CHECK(t)                      \
  ({                                          \
    int __status = (t).callback ();           \
    if (__status == 0)                        \
      printf ("test passed: %s\n", (t).name); \
    else                                      \
      printf ("test failed: %s\n", (t).name); \
    __status;                                 \
  })

int subprocess_run (const char *, char *const *);
void spawn_threads (int, const pthread_attr_t *, void *(*) (void *), void *);

#endif /* XND_TEST_H */
