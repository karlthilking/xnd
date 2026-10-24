/* error.h */
#ifndef XND_ERROR_H
#define XND_ERROR_H

#include "common/compiler.h"

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#ifndef xnd_sys_getpid
# include <sys/syscall.h>
# define xnd_sys_getpid()					\
  ((pid_t)							\
   ({								\
     register long __x0 __asm ("x0");				\
     register long __x16 __asm ("x16") = (long) SYS_getpid;	\
     __asm ("svc #0x80" : "=r" (__x0) : "r" (__x16) : "cc");	\
     __x0;							\
   }))
#endif

#ifdef __FILE_NAME__
# define __XND_FILE__ __FILE_NAME__
#else
# define __XND_FILE__				\
  (__builtin_strrchr (__FILE__, '/')		\
   ? __builtin_strrchr (__FILE__, '/') + 1	\
   : __FILE__)
#endif

#define ANSI_RED 	"\033[0;31m"
#define ANSI_YELLOW 	"\033[0;33m"
#define ANSI_RESET	"\033[0m"

#define xnd_error(fmt, ...)				\
  xnd_error_internal (ANSI_RED, __XND_FILE__, __func__, \
		      __LINE__, fmt, ##__VA_ARGS__)
#define xnd_warn(fmt, ...)					\
  xnd_error_internal (ANSI_YELLOW, __XND_FILE__, __func__,	\
		      __LINE__, fmt, ##__VA_ARGS__)

#define xnd_perror(msg) \
  xnd_perror_internal (msg, errno, __XND_FILE__, __func__, __LINE__)
#define xnd_strerror(msg, errnum) \
  xnd_perror_internal (msg, errnum, __XND_FILE__, __func__, __LINE__)

#define xnd_psignal(sig, msg) \
  xnd_psignal_internal (sig, msg, __XND_FILE__, __func__, __LINE__)

#define xnd_panic(fmt, ...)			\
  do {						\
    xnd_error (fmt, ##__VA_ARGS__);		\
    xnd_abort ();				\
  } while (0)

/* xnd_assert still evalutes the given expression in the absence of
   DEBUG and DEVELOPMENT in order to preserve the result of
   assertions whose expressions contain side-effects  */
#if DEBUG || DEVELOPMENT
# define xnd_assert(e) \
  ((void) \
   (__xnd_likely (e) ? ((void) 0) \
    : xnd_assert_fail (#e, __XND_FILE__, __func__, __LINE__)))
#else /* !(DEBUG || DEVELOPMENT) */
# define xnd_assert(e) ((void) (e))
#endif

/* Formatted output routines used by the error functions. Defaults to
   libc's vdprintf and vsnprintf; modules that cannot use libc stdio
   (libxnd, xnd_restart_internal) overlay these with the stack-based
   routines from io.h via xnd_error_set_io.  */
struct xnd_error_ops
{
  int (*vdprintf) (int, const char *, va_list);
  int (*vsnprintf) (char *, size_t, const char *, va_list);
};

__BEGIN_DECLS

void xnd_error_set_io (const struct xnd_error_ops *) __nonnull__ ((1));

void xnd_error_internal (const char *color, const char *file,
			 const char *func, int line, const char *fmt, ...)
  __attribute_format__ ((__printf__, 5, 6)) __cold;

void xnd_perror_internal (const char *msg, int errnum, const char *file,
			  const char *func, int line) __cold;

void xnd_psignal_internal (int sig, const char *msg, const char *file,
			   const char *func, int line) __cold;

void xnd_assert_fail (const char *expr, const char *file,
		      const char *func, int line) __cold __noreturn;

void xnd_abort (void) __cold __noreturn;

__END_DECLS
#endif /* XND_ERROR_H */
