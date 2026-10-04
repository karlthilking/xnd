/* tst-support.c */
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "common/xthread.h"
#include "common/xalloc.h"
#include "tst-support.h"

static int test_main (int, char **, struct test_config *);
static void signal_handler (int);

int
subprocess_run (const char *path, char *const argv[])
{
  pid_t pid, ret;
  int exit_status, status = 0;

  pid = fork ();
  if (pid < 0)
    err (1, "failed to spawn child process");
  else if (pid == 0)
    {
      execvp (path, argv);
      err (1, "failed to execute program: %s", path);
    }

  do
    {
      ret = waitpid (pid, &status, 0);
      if (ret < 0)
        {
          if (errno != EINTR)
            err (1, "failed to wait on child");
        }
      else if (ret == pid)
        {
          if (WIFEXITED (status))
            exit_status = WEXITSTATUS (status);
          else if (WIFSIGNALED (status))
            exit_status = WTERMSIG (status);
          else
            err (1, "child's status changed without exit or signal\n");
        }
    }
  while (ret != pid);

  return exit_status;
}

void
spawn_threads (int nthrds, const pthread_attr_t *attr,
               void *(*start_routine) (void *), void *arg)
{
  pthread_t *thrds = xmalloc (nthrds * sizeof (*thrds));

  for (int i = 0; i < nthrds; i++)
    xpthread_create (&thrds[i], attr, start_routine, arg);
  for (int i = 0; i < nthrds; i++)
    xpthread_join (thrds[i], NULL);

  free (thrds);
}

static pid_t test_pid = 0;
const char *test_name = NULL;

static void
signal_handler (int sig)
{
  printf ("%s: signaled: %s\n", test_name, strsignal (sig));

  killpg (getpgid (test_pid), sig);
  kill (test_pid, sig);

  signal (sig, SIG_DFL);
  kill (getpid (), sig);
}

static int
test_main (int argc, char **argv, struct test_config *config)
{
  pid_t pid, ret;
  int status, exit_status, sig;
  uint32_t elapsed;
  uint64_t test_start, test_end;

  if (config->prepare_function != NULL)
    config->prepare_function (argc, argv);

  putchar ('\n');
  printf ("%s starting test\n", config->test_name);
  test_start = clock_gettime_nsec_np (CLOCK_UPTIME_RAW);

  pid = fork ();
  if (pid < 0)
    err (1, "failed to fork child process");
  else if (pid == 0)
    {
      setpgid (0, 0);
      if (config->test_function != NULL)
        exit (config->test_function ());
      else if (config->test_function_argv != NULL)
        exit (config->test_function_argv (argc, argv));
      else
        errx (1, "no test function defined");
    }

  test_pid = pid;
  test_name = config->test_name;

  {
    struct sigaction act;
    sigset_t mask;

    sigfillset (&mask);
    act.sa_handler = signal_handler;
    act.sa_mask = mask;
    act.sa_flags = SA_RESETHAND;

    sigaction (SIGINT, &act, NULL);
    sigaction (SIGTERM, &act, NULL);
    sigaction (SIGQUIT, &act, NULL);
  }

  for (;;)
    {
      ret = waitpid (pid, &status, 0);
      if (ret < 0)
        err (1, "failed to wait on child process");
      else if (ret == pid)
        break;
    }

  test_end = clock_gettime_nsec_np (CLOCK_UPTIME_RAW);
  elapsed = (test_end - test_start) / NSEC_PER_MSEC;
  printf ("%s elapsed time: %u ms\n", config->test_name, elapsed);

  if (WIFEXITED (status))
    {
      exit_status = WEXITSTATUS (status);
      printf ("%s exited: %d\n", config->test_name, exit_status);
    }
  else
    {
      assert (WIFSIGNALED (status));
      sig = WTERMSIG (status), exit_status = 128 + sig;
      printf ("%s signaled: %s\n", config->test_name, strsignal (sig));
    }

  putchar ('\n');
  if (config->cleanup_function != NULL)
    config->cleanup_function ();

  return exit_status;
}

int
main (int argc, char *argv[])
{
  struct test_config test_config = { 0 };

#ifdef TEST_NAME
  test_config.test_name = TEST_NAME;
#else
#error "TEST_NAME not defined"
#endif

#ifdef PREPARE_FUNCTION
  test_config.prepare = PREPARE_FUNCTION;
#endif

#if defined(TEST_FUNCTION)
  test_config.test_function = TEST_FUNCTION;
#elif defined(TEST_FUNCTION_ARGV)
  test_config.test_function_argv = TEST_FUNCTION_ARGV;
#endif

#ifdef CLEANUP_FUNCTION
  test_config.cleanup_function = CLEANUP_FUNCTION;
#endif

  exit (test_main (argc, argv, &test_config));
}
