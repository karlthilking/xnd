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

int subprocess_run (const char *, char *const *);
void spawn_threads (int, const pthread_attr_t *,
		    void *(*) (void *), void *);

#endif /* XND_TEST_H */
