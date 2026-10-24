/* xnd_launch.c */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "xnd/xnd.h"
#include "ckptfile.h"
#include "util/path.h"
#include "util/env.h"
#include "util/io.h"
#include "platform/exe.h"
#include "platform/macho.h"
#include "coordinator/xnd_coord_api.h"

#define ARG_IS_HELP(arg) (strncmp (arg, "--help", sizeof ("--help")) == 0)
#define ARG_IS_CKPT_INTERVAL(arg)          \
  (strncmp (arg, "-i", sizeof ("-i")) == 0 \
   || strncmp (arg, "--interval", sizeof ("--interval")) == 0)
#define ARG_IS_USE_ZLIB(arg) \
  (strncmp (arg, "--use-zlib", sizeof ("--use-zlib")) == 0)
#define ARG_IS_CKPT_SIGNAL(arg) \
  (strncmp (arg, "--ckpt-signal", sizeof ("--ckpt-signal")) == 0)
#define ARG_IS_UNLINK_TMP(arg) \
  (strncmp (arg, "--unlink-tmp", sizeof ("--unlink-tmp")) == 0)
#define ARG_IS_INVALID(arg) \
  ((strncmp (arg, "--", 2) == 0) || (strlen (arg) == 2 && (arg)[0] == '-'))

static void usage (const char *, ...)
  __attribute_format__ ((__printf__, 1, 2));
static void xnd_exit (int);
static void prepare_args (int, char **, char **);
static void launch (int, char **);

static const char *help =
  "\n"
  "OVERVIEW: xnd_launch\n"
  "DESCRIPTION: Launch computation under XND\n"
  "USAGE: ./xnd_launch [options] <binary> [args...]\n\n"
  "OPTIONS:\n"
  "  -i, --interval <seconds>\n"
  "       Set checkpoint frequency in seconds\n"
  "       Default: 0 (manual checkpoints only)\n\n"
  "  --use-zlib <option>\n"
  "       Configure using zlib for checkpoint file compression\n"
  "       The two values for <option> are:\n"
  "        - '0', 'false': Don't compress checkpoint files\n"
  "        - '1', 'true': Compress checkpoint files with zlib\n"
  "       Default: 0\n\n"
  "  --ckpt-signal <signal>\n"
  "       Set the signal used for checkpoints. The desired checkpoint\n"
  "       signal should be specified as the integer value of a valid\n"
  "       signal.\n"
  "       Default: SIGUSR2\n\n"
  "  --unlink-tmp <option>\n"
  "       Specify when to unlink temporary arm64 executable, created if\n"
  "       launching an arm64e executable with xnd_launch\n"
  "       The three values for <option> are:\n"
  "        - 'init': Unlink as soon as possible\n"
  "        - 'exit': Unlink at exit (when libxnd's destructor runs)\n"
  "        - 'never': Don't unlink the temporary executable\n"
  "       Default: exit\n\n"
  "  --help\n"
  "       Display this help message\n\n";

int
main (int argc, char *argv[])
{
  const char *opt = NULL, *val = NULL;

  if (argc < 2)
    {
      usage (NULL);
      xnd_exit (XND_EXIT_FAILURE);
    }

#define lshift() *(argc++, argv--)
#define rshift() *(argc--, argv++)
  (void) rshift ();
  while (argc > 0)
    {
      opt = rshift ();
      if (ARG_IS_HELP (opt))
        {
          usage (NULL);
          xnd_exit (XND_EXIT_SUCCESS);
        }
      else if (ARG_IS_CKPT_INTERVAL (opt))
        {
          val = rshift ();
          if (env_set_ckpt_interval (val) != 0)
            {
              usage ("Invalid argument: %s %s\n", opt, val);
              xnd_exit (XND_EXIT_FAILURE);
            }
        }
      else if (ARG_IS_USE_ZLIB (opt))
        {
          val = rshift ();
          if (env_set_zlib_compression (val) != 0)
            {
              usage ("Invalid argument: %s %s\n", opt, val);
              xnd_exit (XND_EXIT_FAILURE);
            }
        }
      else if (ARG_IS_CKPT_SIGNAL (opt))
        {
          val = rshift ();
          if (env_set_ckpt_signal (val) != 0)
            {
              usage ("Invalid argument: %s %s\n", opt, val);
              xnd_exit (XND_EXIT_FAILURE);
            }
        }
      else if (ARG_IS_UNLINK_TMP (opt))
        {
          val = rshift ();
          if (env_set_unlink_tmp (val) != 0)
            {
              usage ("Invalid argument: %s %s\n", opt, val);
              xnd_exit (XND_EXIT_FAILURE);
            }
        }
      else if (ARG_IS_INVALID (opt))
        {
          usage ("Invalid option: %s\n", opt);
          xnd_exit (XND_EXIT_FAILURE);
        }
      else
        {
          (void) lshift ();
          break;
        }
    }
#undef lshift
#undef rshift

  if (argc <= 0)
    {
      usage ("No target executable specified\n");
      xnd_exit (XND_EXIT_FAILURE);
    }
    
  launch (argc, argv);
}

static void
usage (const char *fmt, ...)
{
  if (fmt != NULL)
    {
      va_list ap;
      va_start (ap, fmt);
      vdprintf (STDERR_FILENO, fmt, ap);
      va_end (ap);
    }
  dprintf (STDERR_FILENO, "%s", help);
}

static __noreturn void
xnd_exit (int status)
{
  exit (status);
}

static void
prepare_args (int argc, char **argv, char **new_argv)
{
  int ret;
  char exec_path[PATH_MAX], tmp[PATH_MAX] = { '.', '/', 0 };

  new_argv[argc] = NULL;
  for (uint i = 1u; i < argc; i++)
    new_argv[i] = strdup (argv[i]);

  ret = xnd_path_basename (&tmp[2], sizeof (tmp) - 2, argv[0]);
  if (ret != 0)
    {
      xnd_error ("Failed to get basename of %s\n", argv[0]);
      xnd_exit (XND_EXIT_FAILURE);
    }
  else
    {
      strcat (tmp, "-tmp");
    }

  if (access (argv[0], X_OK) == 0)
    {
      strncpy (exec_path, argv[0], strlen (argv[0]) + 1);
    }
  else
    {
      ret = xnd_path_find (argv[0], exec_path, PATH_MAX);
      if (ret != 0)
        {
          xnd_error ("Failed to find path of %s\n", argv[0]);
          xnd_exit (XND_EXIT_FAILURE);
        }
    }

  ret = binary_arm64e_to_arm64 (exec_path, tmp);
  switch (ret)
    {
    case ARM64E_TO_ARM64_SUCCESS:
      new_argv[0] = strdup (tmp);
      env_set_tmp_binary (tmp);
      break;
    case ARM64E_TO_ARM64_NOT_ARM64E:
      new_argv[0] = strdup (exec_path);
      break;
    case ARM64E_TO_ARM64_FAILURE:
      xnd_error ("Error patching arm64e binary: %s\n", exec_path);
      xnd_exit (XND_EXIT_FAILURE);
    default:
      xnd_error ("Unknown binary_arm64e_to_arm64 return value\n");
      xnd_exit (XND_EXIT_FAILURE);
    }
}

static __noreturn void
launch (int argc, char **argv)
{
  char libxnd_path[PATH_MAX];
  char *new_argv[argc + 1];
  int err;
  pid_t coord_pid;

  prepare_args (argc, argv, new_argv);
  if ((coord_pid = launch_coordinator (false)) == -1)
    {
      xnd_error ("Failed to launch coordinator\n");
      xnd_exit (XND_EXIT_FAILURE);
    }

  err = xnd_exe_path_of (libxnd_path, PATH_MAX, "libxnd.dylib");
  if (err < 0)
    {
      xnd_error ("Failed to get path of libxnd.dylib\n");
      xnd_exit (XND_EXIT_FAILURE);
    }

  env_set_program_name (new_argv[0]);
  xnd_assert (setenv ("DYLD_INSERT_LIBRARIES", libxnd_path, 1) == 0);

  env_set_dyld_shared_region_private ();

  /* the target must never have a vm_reclaim region: xnd_restart_internal
     cannot have one either, and a restored libmalloc that expects one
     traps on its first flush (see env_set_malloc_large_cache_disabled) */
  env_set_malloc_large_cache_disabled ();
  printf ("Executing %s (pid=%d)\n", new_argv[0], getpid ());
  fflush (stdout);

  err = execvp (new_argv[0], new_argv);
  if (err != 0)
    {
      xnd_error ("execvp(%s): %s\n", new_argv[0], strerror (errno));
      xnd_exit (XND_EXIT_FAILURE);
    }

  unreachable ();
}
