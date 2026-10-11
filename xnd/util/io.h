/* io.h */
#ifndef XND_IO_H
#define XND_IO_H

#include <stdarg.h>
#include <stdio.h>
#include "common/compiler.h"

__BEGIN_DECLS

ssize_t writeall (int, const void *, size_t);
ssize_t readall (int, void *, size_t);

int xnd_vsnprintf (char *restrict, size_t, const char *restrict, va_list)
  __format ((__printf__, 3, 0)) __nonnull__ ((1, 3));
int xnd_snprintf (char *restrict, size_t, const char *restrict, ...)
  __format ((__printf__, 3, 4)) __nonnull__ ((1, 3));

int xnd_vdprintf (int, const char *, va_list)
  __format ((__printf__, 2, 0)) __nonnull__ ((2));
int xnd_dprintf (int, const char *, ...)
  __format ((__printf__, 2, 3)) __nonnull__ ((2));

int xnd_vprintf (const char *, va_list)
  __format ((__printf__, 1, 0)) __nonnull__ ((1));
int xnd_printf (const char *, ...)
  __format ((__printf__, 1, 2)) __nonnull__ ((1));

__END_DECLS
#endif /* XND_IO_H */
