/* tst-io.c */
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tst-support.h"
#include "xnd/util/io.h"
#include "common/compiler.h"

#define TEST_NAME "tst-io"

static int test_xnd_snprintf_literal (void);
static int test_xnd_vsnprintf_literal (void);

static int test_xnd_snprintf_string (void);
static int test_xnd_vsnprintf_string (void);

static int test_xnd_snprintf_char (void);
static int test_xnd_vsnprintf_char (void);

static int test_xnd_snprintf_int (void);
static int test_xnd_vsnprintf_int (void);

static int test_xnd_snprintf_uint (void);
static int test_xnd_vsnprintf_uint (void);

static int test_xnd_snprintf_hex (void);
static int test_xnd_vsnprintf_hex (void);

static int test_xnd_snprintf_ptr (void);
static int test_xnd_vsnprintf_ptr (void);

static int test_xnd_snprintf_diverse (void);
static int test_xnd_vsnprintf_diverse (void);

static int test_xnd_snprintf (void);
static int test_xnd_vsnprintf (void);
static int do_test (void);

static const char *strings[] =
  {
    "kaohsiung", "waggery", "monkship", "underdone", "phenanthrene",
    "ineluctability", "sporogenic", "sulfatize", "commemoratory",
    "fulminant", "aerotherapy", "twirl", "latifundium", "javastation",
    "appendicectomy", "homocercal", "areologist", "ras", "sweetsop",
    "trackster", "merchandising", "naturalisation", "loach", "candu",
    "pestilential", "pliskie", "prescient", "microfaction",
    "cosmopolitan", "soon", "generatrix", "sakellaridis", "arithmetize",
    "", "\"", "%%%%", NULL, "0xFFFFFFFF",
  };

static const char chars[] =
  {
    'a', 'Z', '0', '9', 'x', '%', '~', '-', '\\', '"', '\n',
    '\t', '!', '*', '&', 0x1, 0x7F, 0xFE, CHAR_MIN, CHAR_MAX,
  };

static const long long ints[] =
  {
    0, 1, -1, 7, -7, 9, -9, 10, -10, 99, -100, 128, -129, 1000,
    12345, -67890, 999999999, -2000000000, 0x7fffffffLL,
    INT_MAX, INT_MIN, LONG_MAX, LONG_MIN, LLONG_MAX, LLONG_MIN,
  };

static const unsigned long long uints[] =
  {
    0, 1, 2, 9, 10, 15, 16, 17, 100, 255, 256, 4095, 65535, 65536,
    1000000007ULL, 0xfefefefeULL, 0x000000000000000bULL,
    0xabc00097fefe0123ull, INT_MAX, UINT_MAX, ULONG_MAX, ULLONG_MAX,
  };

static void *ptrs[] =
  {
    NULL, (void *) 0x1, (void *) 0xfUL, (void *) 0x10UL,
    (void *) 0x7fff5fbff8c0UL, (void *) 0xdeadbeefUL,
    (void *) UINTPTR_MAX, strings, (void *) chars, (void *) ints,
    (void *) uints,
  };

#define random_choice(list)                     \
  ({                                            \
    uintptr_t __addr = (uintptr_t) (list);      \
    size_t __nmemb, __index;                    \
    __nmemb = (__addr == (uintptr_t) chars      \
               ? countof (chars) - 1            \
               : countof (list));               \
    __index = (size_t) arc4random () % __nmemb; \
    (list)[__index];                            \
  })

#define randchar()	random_choice (chars)
#define randstring()	random_choice (strings)
#define randint()	((int) random_choice (ints))
#define randlong()	((long) random_choice (ints))
#define randllong()	random_choice (ints)
#define randuint()	((unsigned int) random_choice (uints))
#define randulong()	((unsigned long) random_choice (uints))
#define randullong()	random_choice (uints)
#define randptr()	random_choice (ptrs)
    
#define BUFSZ ((size_t) 512)

#define check_fail(fmt, size, msg, ...)                         \
  do {                                                          \
    printf ("check failed: fmt=%s, size=%zu\n", fmt, size);     \
    printf (msg, ##__VA_ARGS__);                                \
  } while (0)

static int
check_return (const char *fmt, size_t size, int val, int exp)
{
  const char msg[] = "xnd_snprintf returned %d, snprintf returned %d\n";

  if (val != exp)
    {
      check_fail (fmt, size, msg, val, exp);
      return 1;
    }
  
  return 0;
}

static int
check_equal (const char *fmt, size_t size, const char *res,
             const char *exp, size_t len)
{
  const char msg[] = "xnd_snprintf result: %s, snprintf result: %s\n";

  if (strncmp (res, exp, len) != 0)
    {
      check_fail (fmt, size, msg, res, exp);
      return 1;
    }

  return 0;
}

static int
check_terminator (const char *fmt, size_t size,
                  const char *res, size_t len)
{
  const size_t index = min (size - 1, len);
  const char msg[] = "xnd_snprintf missed null terminator: %s\n";

  if (res[index] != '\0')
    {
      check_fail (fmt, size, msg, res);
      return 1;
    }

  return 0;
}

__unused static int
__VSNPRINTF_CHECK (const char *fmt, ...)
{
  va_list ap, aq;
  int r1, r2;
  char s1[BUFSZ], s2[BUFSZ];
  const size_t n = BUFSZ;

  va_start (ap, fmt);
  va_copy (aq, ap);
  r1 = xnd_vsnprintf (s1, n, fmt, ap);
  r2 = vsnprintf (s2, n, fmt, aq);
  va_end (aq);
  va_end (ap);

  if (check_return (fmt, n, r1, r2)
      || check_equal (fmt, n, s1, s2, (size_t) r1)
      || check_terminator (fmt, n, s1, (size_t) r1))
    return 1;

  return 0;
}

#define __SNPRINTF_CHECK(fmt, ...)                              \
  ({                                                            \
    int __r1, __r2, __ret = 0;                                  \
    char __s1[BUFSZ], __s2[BUFSZ];                              \
    const size_t __n = BUFSZ;                                   \
    do {                                                        \
      __r1 = xnd_snprintf (__s1, __n, fmt, ##__VA_ARGS__);      \
      __r2 = snprintf (__s2, __n, fmt, ##__VA_ARGS__);          \
    } while (0);                                                \
    const size_t __len = (size_t) __r1;                         \
    if (check_return (fmt, __n, __r1, __r2)                     \
        || check_equal (fmt, __n, __s1, __s2, __len)            \
        || check_terminator (fmt, __n, __s1, __len))            \
      __ret = 1;                                                \
    __ret;                                                      \
  })

#define __XSNPRINTF_CHECK(...)	| __SNPRINTF_CHECK (__VA_ARGS__)
#define __XVSNPRINTF_CHECK(...)	| __VSNPRINTF_CHECK (__VA_ARGS__)

#define SNPRINTF_CHECK(list) 	0 list (__XSNPRINTF_CHECK)
#define VSNPRINTF_CHECK(list)	0 list (__XVSNPRINTF_CHECK)

#define STRING_LITERAL_LIST(X)                  \
  X ("")                                        \
  X (" ")                                       \
  X ("\n")                                      \
  X ("\t\n\t\r\n")                              \
  X ("%%")                                      \
  X ("%%%%")                                    \
  X ("0123456789ABCDEF")                        \
  X ("%%s%%c%%d")

static int
test_xnd_snprintf_literal (void)
{
  return SNPRINTF_CHECK (STRING_LITERAL_LIST);
}

static int
test_xnd_vsnprintf_literal (void)
{
  return VSNPRINTF_CHECK (STRING_LITERAL_LIST);
}

#define STRING_LIST(X)                          \
  X ("%s", s1)                                  \
  X ("%s%%%s", s1, s1)                          \
  X ("%s-%s", s1, s2)                           \
  X ("%s%s%s", s1, s2, s1)                      \
  X ("%s%%s", s1)

static int
test_xnd_snprintf_string (void)
{
  int ret = 0;
  const char *s1, *s2;

  for (size_t i = 0; i < countof (strings); i++)
    {
      s1 = strings[i], s2 = strings[countof (strings) - 1 - i];
      ret |= SNPRINTF_CHECK (STRING_LIST);
    }

  return ret;
}

static int
test_xnd_vsnprintf_string (void)
{
   int ret = 0;
  const char *s1, *s2;

  for (size_t i = 0; i < countof (strings); i++)
    {
      s1 = strings[i], s2 = strings[countof (strings) - 1 - i];
      ret |= VSNPRINTF_CHECK (STRING_LIST);
    }

  return ret;
}

#define CHAR_LIST(X)                            \
  X ("%c", c1)                                  \
  X ("%c-%c", c1, c2)                           \
  X ("%c%c%c", c1, c1, c1)                      \
  X ("%c%%%c", c1, c2)                          \
  X ("-- %c -- %c --\n", c1, c2)

static int
test_xnd_snprintf_char (void)
{
  int ret = 0;
  char c1, c2;
  
  for (size_t i = 0; i < countof (chars) - 1; i++)
    {
      c1 = chars[i], c2 = chars[countof (chars) - 2 - i];
      ret |= SNPRINTF_CHECK (CHAR_LIST);
    }

  return ret;
}

static int
test_xnd_vsnprintf_char (void)
{
  int ret = 0;
  char c1, c2;
  
  for (size_t i = 0; i < countof (chars) - 1; i++)
    {
      c1 = chars[i], c2 = chars[countof (chars) - 2 - i];
      ret |= VSNPRINTF_CHECK (CHAR_LIST);
    }

  return ret;
}

#define INT_INNER_LIST(X)                      \
  X ("%d%i", d, d)                             \
  X ("%ld%li", ld, ld)                         \
  X ("%lld%lli", lld, lld)                     \
  X ("%zd %zi", ld, ld)                        \
  X ("%d %ld %lld", d, ld, lld)

#define INT_OUTER_LIST(X)                           \
  X ("%d%d%d", -1, 0, 1)                            \
  X ("%d %d", INT_MIN, INT_MAX)                     \
  X ("%ld %ld", LONG_MIN, LONG_MAX)                 \
  X ("%lld %lld", LLONG_MIN, LLONG_MAX)             \
  X ("%zd %zd", LONG_MIN, LONG_MAX)                 \
  X ("%i %li %lli", INT_MAX, LONG_MAX, LLONG_MAX)

static int
test_xnd_snprintf_int (void)
{
  int d, ret = 0;
  long ld;
  long long lld;

  for (size_t i = 0; i < countof (ints); i++)
    {
      lld = ints[i], ld = (long) lld, d = (int) ld;
      ret |= SNPRINTF_CHECK (INT_INNER_LIST);
    }

  ret |= SNPRINTF_CHECK (INT_OUTER_LIST);
  return ret;
}

static int
test_xnd_vsnprintf_int (void)
{
  int d, ret = 0;
  long ld;
  long long lld;

  for (size_t i = 0; i < countof (ints); i++)
    {
      lld = ints[i], ld = (long) lld, d = (int) ld;
      ret |= VSNPRINTF_CHECK (INT_INNER_LIST);
    }

  ret |= VSNPRINTF_CHECK (INT_OUTER_LIST);
  return ret;
}

#define UINT_INNER_LIST(X)             \
  X ("%u", u)                          \
  X ("%lu", ul)                        \
  X ("%llu", ull)                      \
  X ("%zu", ul)                        \
  X ("%u %lu %llu", u, ul, ull)

#define UINT_OUTER_LIST(X)                             \
  X ("%u %lu", UINT_MAX, ULONG_MAX)                    \
  X ("%lu %llu", ULONG_MAX, ULLONG_MAX)                \
  X ("%u %lu %llu", UINT_MAX, ULONG_MAX, ULLONG_MAX)   \
  X ("%zu", ULONG_MAX) 

static int
test_xnd_snprintf_uint (void)
{
  int ret = 0;
  unsigned int u;
  unsigned long ul;
  unsigned long long ull;

  for (size_t i = 0; i < countof (uints); i++)
    {
      ull = uints[i], ul = (unsigned long) ull, u = (unsigned int) ul;
      ret |= SNPRINTF_CHECK (UINT_INNER_LIST);
    }

  ret |= SNPRINTF_CHECK (UINT_OUTER_LIST);
  return ret;
}

static int
test_xnd_vsnprintf_uint (void)
{
  int ret = 0;
  unsigned int u;
  unsigned long ul;
  unsigned long long ull;

  for (size_t i = 0; i < countof (uints); i++)
    {
      ull = uints[i], ul = (unsigned long) ull, u = (unsigned int) ul;
      ret |= VSNPRINTF_CHECK (UINT_INNER_LIST);
    }

  ret |= VSNPRINTF_CHECK (UINT_OUTER_LIST);
  return ret;
}

#define HEX_INNER_LIST(X)             \
  X ("%x", u)                         \
  X ("%lx", ul)                       \
  X ("%llx", ull)                     \
  X ("%x%X", u, u)                    \
  X ("%llX", ull)                     \
  X ("%llx", ull >> 16)               \
  X ("%x %lx %llx", u, ul, ull)       \
  X ("%X %lX %llX", u, ul, ull)

#define HEX_OUTER_LIST(X)                               \
  X ("%x %X", UINT_MAX, UINT_MAX)                       \
  X ("%lx %lX", ULONG_MAX, ULONG_MAX)                   \
  X ("%llx %llX", ULLONG_MAX, ULLONG_MAX)               \
  X ("%x %lx %llx", UINT_MAX, ULONG_MAX, ULLONG_MAX)

static int
test_xnd_snprintf_hex (void)
{
  int ret = 0;
  unsigned int u;
  unsigned long ul;
  unsigned long long ull;
  
  for (size_t i = 0; i < countof (uints); i++)
    {
      ull = uints[i], ul = (unsigned long) ull, u = (unsigned int) ul;
      ret |= SNPRINTF_CHECK (HEX_INNER_LIST);
    }

  ret |= SNPRINTF_CHECK (HEX_OUTER_LIST);
  return ret;
}

static int
test_xnd_vsnprintf_hex (void)
{
  int ret = 0;
  unsigned int u;
  unsigned long ul;
  unsigned long long ull;
  
  for (size_t i = 0; i < countof (uints); i++)
    {
      ull = uints[i], ul = (unsigned long) ull, u = (unsigned int) ul;
      ret |= VSNPRINTF_CHECK (HEX_INNER_LIST);
    }

  ret |= VSNPRINTF_CHECK (HEX_OUTER_LIST);
  return ret;
}

#define POINTER_LIST(X)      \
  X ("%p", p1)               \
  X ("%p%p", p1, p2)         \
  X ("%p%%p", p1)            \
  X ("%p%%%p", p1, p2)       \
  X ("%p-%p", p1, p2)

static int
test_xnd_snprintf_ptr (void)
{
  int ret = 0;
  void *p1, *p2;

  for (size_t i = 0; i < countof (ptrs); i++)
    {
      p1 = ptrs[i], p2 = ptrs[countof (ptrs) - 1 - i];
      ret |= SNPRINTF_CHECK (POINTER_LIST);
    }

  return ret;
}

static int
test_xnd_vsnprintf_ptr (void)
{
  int ret = 0;
  void *p1, *p2;

  for (size_t i = 0; i < countof (ptrs); i++)
    {
      p1 = ptrs[i], p2 = ptrs[countof (ptrs) - 1 - i];
      ret |= VSNPRINTF_CHECK (POINTER_LIST);
    }

  return ret;
}

#define DIVERSE_LIST(X)                                    \
  X ("(+ (- %u %d) %lld)\n", _7.u, _4.d, _6.lld)           \
  X ("%c-%c %p %llu\n", _1.c, _1.c, _3.p, _9.llu)          \
  X ("%s (%p)\t\t %d/%c\n", _2.s, _3.p, _4.d, _1.c)        \
  X ("[%ld..%lu]\n", _5.ld, _8.lu)

static int
test_xnd_snprintf_diverse (void)
{
  int ret = 0;
  union fmtarg_u
  {
    char c;
    const char *s;
    void *p;
    int d;
    long ld;
    long long lld;
    unsigned int u;
    unsigned long lu;
    unsigned long long llu;
  } _1, _2, _3, _4, _5, _6, _7, _8, _9;

  for (int i = 0; i < 512; i++)
    {
      _1.c = randchar (), _2.s = randstring (), _3.p = randptr ();
      _4.d = randint (), _5.ld = randlong (), _6.lld = randllong ();
      _7.u = randuint (), _8.lu = randulong (), _9.llu = randullong ();
      ret |= SNPRINTF_CHECK (DIVERSE_LIST);
    }

  return ret;
}

static int
test_xnd_vsnprintf_diverse (void)
{
  int ret = 0;
  union fmtarg_u
  {
    char c;
    const char *s;
    void *p;
    int d;
    long ld;
    long long lld;
    unsigned int u;
    unsigned long lu;
    unsigned long long llu;
  } _1, _2, _3, _4, _5, _6, _7, _8, _9;

  for (int i = 0; i < 512; i++)
    {
      _1.c = randchar (), _2.s = randstring (), _3.p = randptr ();
      _4.d = randint (), _5.ld = randlong (), _6.lld = randllong ();
      _7.u = randuint (), _8.lu = randulong (), _9.llu = randullong ();
      ret |= VSNPRINTF_CHECK (DIVERSE_LIST);
    }

  return ret;
}

static int
test_xnd_snprintf (void)
{
  struct subtest tests[] =
    {
      SUBTEST_DECL (test_xnd_snprintf_literal),
      SUBTEST_DECL (test_xnd_snprintf_string),
      SUBTEST_DECL (test_xnd_snprintf_char),
      SUBTEST_DECL (test_xnd_snprintf_int),
      SUBTEST_DECL (test_xnd_snprintf_uint),
      SUBTEST_DECL (test_xnd_snprintf_hex),
      SUBTEST_DECL (test_xnd_snprintf_ptr),
      SUBTEST_DECL (test_xnd_snprintf_diverse),
    };

  int ret = 0;
  unsigned int cnt = 0, total = countof (tests);

  for (unsigned int i = 0; i < total; i++)
    {
      ret = SUBTEST_CHECK (tests[i]);
      cnt += !!(ret == 0);
    }

  return (cnt < total);
}

static int
test_xnd_vsnprintf (void)
{
  struct subtest tests[] =
    {
      SUBTEST_DECL (test_xnd_vsnprintf_literal),
      SUBTEST_DECL (test_xnd_vsnprintf_string),
      SUBTEST_DECL (test_xnd_vsnprintf_char),
      SUBTEST_DECL (test_xnd_vsnprintf_int),
      SUBTEST_DECL (test_xnd_vsnprintf_uint),
      SUBTEST_DECL (test_xnd_vsnprintf_hex),
      SUBTEST_DECL (test_xnd_vsnprintf_ptr),
      SUBTEST_DECL (test_xnd_vsnprintf_diverse),
    };

  int ret = 0;
  unsigned int cnt = 0, total = countof (tests);

  for (unsigned int i = 0; i < total; i++)
    {
      ret = SUBTEST_CHECK (tests[i]);
      cnt += !!(ret == 0);
    }

  return (cnt < total);
}

static int
do_test (void)
{
  return (test_xnd_snprintf () || test_xnd_vsnprintf ());
}

#define TEST_FUNCTION do_test
#include "tst-support.c"
