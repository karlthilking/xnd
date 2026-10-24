/* env.c */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "xnd/xnd.h"
#include "env.h"

static __always_inline void
env_set_pid (const char *var, pid_t pid)
{
  char buf[11];

  snprintf (buf, sizeof (buf), "%d", pid);
  xnd_assert (setenv (var, buf, 1) == 0);
}

static __always_inline pid_t
env_get_pid (const char *var)
{
  char *value;
  pid_t pid;

  xnd_assert ((value = getenv (var)) != NULL);
  pid = atoi (value);

  return pid;
}

void
env_set_pid_info (pid_t virt_pid, pid_t real_pid, pid_t virt_ppid,
                  pid_t real_ppid)
{
  if (virt_pid != -1)
    env_set_pid (XND_VIRTUAL_PID_ENV, virt_pid);

  if (real_pid != -1)
    env_set_pid (XND_REAL_PID_ENV, real_pid);

  if (virt_ppid != -1)
    env_set_pid (XND_VIRTUAL_PPID_ENV, virt_ppid);

  if (real_ppid != -1)
    env_set_pid (XND_REAL_PPID_ENV, real_ppid);
}

void
env_get_pid_info (pid_t *virt_pid, pid_t *real_pid, pid_t *virt_ppid,
                  pid_t *real_ppid)
{
  if (virt_pid)
    *virt_pid = env_get_pid (XND_VIRTUAL_PID_ENV);

  if (real_pid)
    *real_pid = env_get_pid (XND_REAL_PID_ENV);

  if (virt_ppid)
    *virt_ppid = env_get_pid (XND_VIRTUAL_PPID_ENV);

  if (real_ppid)
    *real_ppid = env_get_pid (XND_REAL_PPID_ENV);
}

void
env_set_program_name (char *value)
{
  char path[PATH_MAX], *name = NULL;

  realpath (value, path);
  name = (strrchr (path, '/') ? strrchr (path, '/') + 1 : path);
  setenv (XND_PROGRAM_ENV, name, 1);
}

char *
env_get_program_name (void)
{
  return getenv (XND_PROGRAM_ENV);
}

void
env_set_tmp_binary (char *value)
{
  setenv (XND_TMP_BINARY_ENV, value, 1);
}

char *
env_get_tmp_binary (void)
{
  return getenv (XND_TMP_BINARY_ENV);
}

int
env_set_unlink_tmp (const char *val)
{
  if (val == NULL)
    return -1;
  if (strcmp (val, "init") && strcmp (val, "exit")
      && strcmp (val, "never"))
    return -1;

  setenv (XND_UNLINK_TMP_ENV, val, 1);
  return 0;
}

char *
env_get_unlink_tmp (void)
{
  return getenv (XND_UNLINK_TMP_ENV);
}

int
env_set_ckpt_signal (const char *val)
{
  int sig;
  char buf[11];

  sig = (val != NULL ? atoi (val) : -1);
  if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP)
    return -1;

  snprintf (buf, sizeof (buf), "%d", sig);
  setenv (XND_CKPT_SIGNAL_ENV, buf, 1);
  return 0;
}

int
env_get_ckpt_signal (void)
{
  char *val;
  static int ckptsig = -1;

  if (__xnd_unlikely (ckptsig == -1))
    {
      ckptsig = XND_DEFAULT_CKPT_SIGNAL;
      val = getenv (XND_CKPT_SIGNAL_ENV);
      if (val != NULL)
        ckptsig = atoi (val);
    }

  return ckptsig;
}

void
env_set_dyld_shared_region_private (void)
{
  xnd_assert (setenv ("DYLD_SHARED_REGION", "private", 1) == 0);
}

void
env_unset_dyld_shared_region (void)
{
  xnd_assert (unsetenv ("DYLD_SHARED_REGION") == 0);
}

/* Prevent libmalloc from allocating its deferred reclaim ring (the
   vm_reclaim region). The kernel maps it permanently at the lowest
   free address, inside the range the restored application occupies,
   so xnd_restart_internal must run without it. The application must
   then run without it as well (set by xnd_launch): after restart the
   task has no ring registered with the kernel, and libmalloc traps in
   _xzm_reclaim_force_sync when a flush of its ring fails. */
void
env_set_malloc_large_cache_disabled (void)
{
  xnd_assert (setenv ("MallocLargeCache", "0", 1) == 0);
}

char *
env_get_dyld_shared_region (void)
{
  return getenv ("DYLD_SHARED_REGION");
}

bool
env_dyld_shared_region_is_private (void)
{
  char *value;

  value = getenv ("DYLD_SHARED_REGION");
  if (value == NULL || strcmp (value, "private"))
    return false;

  return true;
}

int
env_set_zlib_compression (const char *val)
{
  int ret = -1;

  if (val != NULL)
    {
      if (strcmp (val, "0") == 0 || strcmp (val, "false") == 0)
        {
          ret = 0;
          setenv (XND_USE_ZLIB_ENV, "0", 1);
        }
      else if (strcmp (val, "1") == 0 || strcmp (val, "true") == 0)
        {
          ret = 0;
          setenv (XND_USE_ZLIB_ENV, "1", 1);
        }
    }

  return ret;
}

bool
env_use_zlib_compression (void)
{
  char *value;

  value = getenv (XND_USE_ZLIB_ENV);
  if (value && atoi (value) != 0)
    return true;

  return false;
}

int
env_set_ckpt_interval (const char *val)
{
  int secs;

  secs = (val != NULL ? atoi (val) : -1);
  if (secs < 0)
    return -1;

  setenv (XND_CKPT_INTERVAL_ENV, val, 1);
  return 0;
}

int
env_get_ckpt_interval (void)
{
  int secs;
  char *val = NULL;

  val = getenv (XND_CKPT_INTERVAL_ENV);
  secs = (val != NULL ? atoi (val) : 0);

  return secs;
}
