/* log.c */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "xnd/xnd.h"
#include "xnd/fd_table.h"
#include "xnd/util/io.h"
#include "xnd/util/log.h"

#define LOG_DIR "/tmp/xnd"
#define LOG_PATH_MAX (const_strlen (LOG_DIR "/") + 10 + sizeof (".log"))

/* truncate an existing log file not modified in this many seconds */
#define LOG_STALE_SECS (60 * 30)

extern u32 xnd_pid;

static int log_fd = -1;
static char log_path[LOG_PATH_MAX];

/* whether this process created the log file; children share the
   file after fork, but only its creator removes it (this survives
   checkpoint-restart, unlike the real pid) */
static bool log_owner = false;

static u32
log_pid (void)
{
  return (xnd_pid != 0 ? xnd_pid : (u32) xnd_sys_getpid ());
}

static bool
log_stale (const char *path)
{
  struct stat st;
  struct timespec now;

  if (stat (path, &st) != 0)
    return true;

  clock_gettime (CLOCK_REALTIME, &now);
  return ((now.tv_sec - st.st_mtime) >= LOG_STALE_SECS);
}

void
xnd_log_setup (void)
{
  int ret, oflag = O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC;
  const mode_t mode = 0666;

  ret = xnd_snprintf (log_path, sizeof (log_path), LOG_DIR "/%d.log",
                      xnd_sys_getpid ());
  xnd_assert (ret < sizeof (log_path));

  if (mkdir (LOG_DIR, 0777) != 0 && errno != EEXIST)
    {
      xnd_perror ("mkdir");
      return;
    }

  if (log_stale (log_path))
    oflag |= O_TRUNC;

  log_fd = open (log_path, oflag, mode);
  if (log_fd < 0)
    {
      xnd_perror ("open");
      return;
    }

  /* track the log file so it is reopened on restart */
  fd_table_open_priv (log_fd, log_path, oflag, mode);
  log_owner = true;
}

void
xnd_log_cleanup (void)
{
  if (log_fd == -1)
    return;

  if (close (log_fd) == 0)
    fd_table_close_priv (log_fd);
  log_fd = -1;

  if (log_owner)
    (void) unlink (log_path);
}

void
xnd_log_atfork_child (void)
{
  log_owner = false;
}

void
xnd_log_printf (const char *fmt, ...)
{
  char buf[512];
  size_t len;
  int ret;
  va_list ap;

  if (log_fd == -1)
    return;

  /* prefix with the virtual pid (stable across checkpoint-restart)
     since processes share the log file after fork, and write the
     line in a single write(2) so lines do not interleave */
  len = (size_t) xnd_snprintf (buf, sizeof (buf), "[%u] ", log_pid ());
  va_start (ap, fmt);
  ret = xnd_vsnprintf (buf + len, sizeof (buf) - len, fmt, ap);
  va_end (ap);

  len += min ((size_t) ret, sizeof (buf) - len - 1);
  (void) writeall (log_fd, buf, len);
}
