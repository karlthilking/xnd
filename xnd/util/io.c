/* io.c */
#include "xnd/xnd.h"
#include "xnd/util/io.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>

ssize_t
writeall (int fd, const void *buf, size_t nbyte)
{
  ssize_t ret;
  size_t bytes = 0;
  const char *ptr = (const char *) buf;

  do
    {
    retry:
      ret = write (fd, ptr + bytes, nbyte - bytes);
      if ((ret == -1) && (errno == EINTR))
	goto retry;
    }
  while ((ret != -1) && ((bytes += ret) < nbyte));
      
  return bytes;
}

ssize_t
readall (int fd, void *buf, size_t nbyte)
{
  ssize_t ret;
  size_t bytes = 0;
  char *ptr = (char *) buf;

  do
    {
    retry:
      ret = read (fd, ptr + bytes, nbyte - bytes);
      if ((ret == -1) && (errno == EINTR))
	goto retry;
    }
  while ((ret > 0) && ((bytes += ret) < nbyte));

  return bytes;
}

struct iobuf
{
  char *buf;
  size_t len;
  size_t size;
};

static inline void iobuf_finalize (struct iobuf *buf)
  __nonnull__ ((1));	
static inline void iobuf_putchar (struct iobuf *buf, char c)
  __nonnull__ ((1));
static inline void iobuf_putstrn (struct iobuf *restrict buf,
				  const char *restrict str, size_t len)
  __nonnull__ ((1, 2));
static inline void iobuf_putstr (struct iobuf *restrict buf,
				 const char *restrict str)
  __nonnull__ ((1, 2));
static inline void iobuf_putudec (struct iobuf *buf, unsigned long long n)
  __nonnull__ ((1));
static inline void iobuf_putdec (struct iobuf *buf, long long n)
  __nonnull__ ((1));
static inline void iobuf_puthex (struct iobuf *restrict buf,
				 unsigned long long n,
				 bool upper, bool prefix)
  __nonnull__ ((1));

static inline void
iobuf_finalize (struct iobuf *buf)
{
  const size_t index = min (buf->len, buf->size - 1);
  buf->buf[index] = '\0';
}

static inline void
iobuf_putchar (struct iobuf *buf, char c)
{
  if (buf->len + 1 < buf->size)
    *(buf->buf + buf->len) = c;

  buf->len++;
}

static inline void
iobuf_putstrn (struct iobuf *restrict buf, const char *restrict str,
	       size_t len)
{
  size_t n;

  if (buf->len + 1 < buf->size)
    {
      n = min (len, buf->size - buf->len - 1);
      for (size_t i = 0; i < n; i++)
	*(buf->buf + buf->len + i) = *(str + i);
    }

  buf->len += len;
}

static inline void
iobuf_putstr (struct iobuf *restrict buf, const char *restrict str)
{
  iobuf_putstrn (buf, str, strlen (str));
}

static inline void
iobuf_putudec (struct iobuf *buf, unsigned long long n)
{
  char str[32], *p = str + sizeof (str);
  unsigned long long rem;

  *--p = '\0';
  if (n != 0)
    {
      while (n != 0)
	{
	  rem = n % 10;
	  n /= 10;
	  *--p = rem + '0';
	}
    }
  else
    *--p = '0';

  iobuf_putstr (buf, p);
}

static inline void
iobuf_putdec (struct iobuf *buf, long long n)
{
  char str[32], *p = str + sizeof (str);
  bool neg;
  unsigned long long rem, u;

  if (n < 0)
    {
      neg = true;
      u = -(unsigned long long) n;
    }
  else
    {
      neg = false;
      u = (unsigned long long) n;
    }

  *--p = '\0';
  if (u != 0)
    {
      while (u != 0)
	{
	  rem = u % 10;
	  u /= 10;
	  *--p = rem + '0';
	}
    }
  else
    *--p = '0';

  if (neg)
    iobuf_putchar (buf, '-');

  iobuf_putstr (buf, p);
}

static inline void
iobuf_puthex (struct iobuf *restrict buf, unsigned long long n,
	      bool upper, bool prefix)
{
  char str[32], *p = str + sizeof (str);
  const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";

  *--p = '\0';
  if (n != 0)
    {
      while (n != 0)
	{
	  *--p = hex[n & 0xF];
	  n >>= 4;
	}
    }
  else
    *--p = '0';

  if (prefix)
    iobuf_putstr (buf, "0x");

  iobuf_putstr (buf, p);
}

int
xnd_vsnprintf (char *restrict str, size_t size,
	       const char *restrict fmt, va_list ap)
{
  union __fmtarg_u
  {
    char _c;
    char *_s;
    int _i;
    long _l;
    long long _ll;
    unsigned int _ui;
    unsigned long _ul;
    unsigned long long _ull;
  };

  unsigned int lcnt;
  bool upper;
  char *fp = NULL;
  union __fmtarg_u arg;
  struct iobuf buf =
    {
      .buf = str,
      .len = 0,
      .size = size
    };

  if (size > 0)
    buf.buf[0] = '\0';

  while (*fmt != '\0')
    {
      arg._ull = 0;
      lcnt = 0;
      fp = strchr (fmt, '%');
      if (fp == NULL)
	{
	  iobuf_putstr (&buf, fmt);
	  break;
	}

      /* output up until next format specifier */
      iobuf_putstrn (&buf, fmt, fp - fmt);
      fmt = fp + 1;

      /* "%%" translated to literal '%' */
      if (*fmt == '%')
	{
	  iobuf_putchar (&buf, '%');
	  fmt++;
	  continue;
	}

      for (;;)
	{
	  switch (*fmt)
	    {
	    case 'c':
	      arg._c = (char) va_arg (ap, int);
	      iobuf_putchar (&buf, arg._c);
	      break;
	    case 'd':
	    case 'i':
	      switch (lcnt)
		{
		case 0:
		  arg._i = va_arg (ap, int);
		  iobuf_putdec (&buf, arg._i);
		  break;
		case 1:
		  arg._l = va_arg (ap, long);
		  iobuf_putdec (&buf, arg._l);
		  break;
		default:
		  arg._ll = va_arg (ap, long long);
		  iobuf_putdec (&buf, arg._ll);
		  break;
		}
	      break;
	    case 'l':
	      lcnt++;
	      fmt++;
	      continue;
	    case 'z':
	      /* argument is size of size_t */
#if SSIZE_MAX == INT_MAX
	      lcnt = 0;
#elif SSIZE_MAX == LONG_MAX
	      lcnt = 1;
#elif SSIZE_MAX == LLONG_MAX
	      lcnt = 2;
#endif
	      fmt++;
	      continue;
	    case 'p':
	      /* prefix with "0x", lowercase hexadecimal */
	      arg._ull = (unsigned long long) va_arg (ap, void *);
	      iobuf_puthex (&buf, arg._ull, false, true);
	      break;
	    case 's':
	      arg._s = va_arg (ap, char *);
	      iobuf_putstr (&buf, arg._s ? arg._s : "(null)");
	      break;
	    case 'u':
	      switch (lcnt)
		{
		case 0:
		  arg._ui = va_arg (ap, unsigned int);
		  iobuf_putudec (&buf, arg._ui);
		  break;
		case 1:
		  arg._ul = va_arg (ap, unsigned long);
		  iobuf_putudec (&buf, arg._ul);
		  break;
		default:
		  arg._ull = va_arg (ap, unsigned long long);
		  iobuf_putudec (&buf, arg._ull);
		  break;
		}
	      break;
	    case 'x':
	    case 'X':
	      /* uppercase hexadecimal iff *fmt == 'X',
		 not prefixed with "0x" */
	      upper = (*fmt == 'X');
	      switch (lcnt)
		{
		case 0:
		  arg._ui = va_arg (ap, unsigned int);
		  iobuf_puthex (&buf, arg._ui, upper, false);
		  break;
		case 1:
		  arg._ul = va_arg (ap, unsigned long);
		  iobuf_puthex (&buf, arg._ul, upper, false);
		  break;
		default:
		  arg._ull = va_arg (ap, unsigned long long);
		  iobuf_puthex (&buf, arg._ull, upper, false);
		  break;
		}
	      break;
	    default:
	      iobuf_putchar (&buf, *fmt);
	      break;
	    }
	  break;
	}
      if (*fmt++ == '\0')
	break;
    }

  /* Return total length of the string that would have been created
     with unlimited space, even if the resulting string had to be
     truncated */
  iobuf_finalize (&buf);
  return (int) buf.len;
}

int
xnd_snprintf (char *restrict str, size_t size,
	      const char *restrict fmt, ...)
{
  int ret;
  va_list ap;

  va_start (ap, fmt);
  ret = xnd_vsnprintf (str, size, fmt, ap);
  va_end (ap);

  return ret;
}

int
xnd_vdprintf (int fd, const char *fmt, va_list ap)
{
  char *buf;
  size_t len, size = 512;

  buf = alloca (size);
  len = (size_t) xnd_vsnprintf (buf, size, fmt, ap);
  (void) writeall (fd, buf, min (len, size - 1));

  return (int) len;
}

int
xnd_dprintf (int fd, const char *fmt, ...)
{
  int ret;
  va_list ap;

  va_start (ap, fmt);
  ret = xnd_vdprintf (fd, fmt, ap);
  va_end (ap);

  return ret;
}

int
xnd_vprintf (const char *fmt, va_list ap)
{
  return xnd_vdprintf (STDOUT_FILENO, fmt, ap);
}

int
xnd_printf (const char *fmt, ...)
{
  int ret;
  va_list ap;

  va_start (ap, fmt);
  ret = xnd_vdprintf (STDOUT_FILENO, fmt, ap);
  va_end (ap);

  return ret;
}
