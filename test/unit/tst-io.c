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
static int test_xnd_snprintf_string (void);
static int test_xnd_snprintf_char (void);
static int test_xnd_snprintf_int (void);
static int test_xnd_snprintf_uint (void);
static int test_xnd_snprintf_hex (void);
static int test_xnd_snprintf_ptr (void);
static int test_xnd_snprintf_diverse (void);
static int test_xnd_snprintf (void);
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

#define SNPRINTF_CHECK(fmt, ...)                                \
  ({                                                            \
    int __rv, __re, __status = 0;                               \
    const size_t __n = BUFSZ;                                   \
    char __res[BUFSZ], __exp[BUFSZ];                            \
    __rv = xnd_snprintf (__res, __n, fmt, ##__VA_ARGS__);       \
    __re = snprintf (__exp, __n, fmt, ##__VA_ARGS__);           \
    if (__rv != __re)                                           \
      {                                                         \
        printf ("check failed: fmt=\"%s\", size=%zu\n"          \
                "xnd_snprintf returned %d\n"                    \
                "snprintf returned %d\n",                       \
                fmt, __n, __rv, __re);                          \
        __status = -1;                                          \
      }                                                         \
    else if (strncmp (__res, __exp, (size_t) __rv) != 0)        \
      {                                                         \
        printf ("check failed: fmt=\"%s\", size=%zu\n"          \
                "xnd_snprintf result: %s\n"                     \
                "snprintf result: %s\n",                        \
                fmt, __n, __res, __exp);                        \
        __status = -1;                                          \
      }                                                         \
    __status;                                                   \
  })

static int
test_xnd_snprintf_literal (void)
{
  return 0
    | SNPRINTF_CHECK ("")
    | SNPRINTF_CHECK (" ")
    | SNPRINTF_CHECK ("\n")
    | SNPRINTF_CHECK ("\t\n\t\r\n")
    | SNPRINTF_CHECK ("%%")
    | SNPRINTF_CHECK ("%%%%")
    | SNPRINTF_CHECK ("0123456789ABCDEF")
    | SNPRINTF_CHECK ("%%s%%c%%d");
}

static int
test_xnd_snprintf_string (void)
{
  int ret = 0;
  const char *s1, *s2;

  for (size_t i = 0; i < countof (strings); i++)
    {
      s1 = strings[i], s2 = strings[countof (strings) - 1 - i];
      ret |= 0
        | SNPRINTF_CHECK ("%s", s1)
        | SNPRINTF_CHECK ("%s%%%s", s1, s1)
        | SNPRINTF_CHECK ("%s-%s", s1, s2)
        | SNPRINTF_CHECK ("%s%s%s", s1, s2, s1)
        | SNPRINTF_CHECK ("%s%%s", s1);
    }

  return ret;
}

static int
test_xnd_snprintf_char (void)
{
  int ret = 0;
  char c1, c2;
  
  for (size_t i = 0; i < countof (chars) - 1; i++)
    {
      c1 = chars[i], c2 = chars[countof (chars) - 2 - i];
      ret |= 0
        | SNPRINTF_CHECK ("%c", c1)
        | SNPRINTF_CHECK ("%c-%c", c1, c2)
        | SNPRINTF_CHECK ("%c%c%c", c1, c1, c1)
        | SNPRINTF_CHECK ("%c%%%c", c1, c2)
        | SNPRINTF_CHECK ("-- %c -- %c --\n", c1, c2);
    }

  return ret;
}

static int
test_xnd_snprintf_int (void)
{
  int d, ret = 0;
  long ld;
  long long lld;
  
  for (size_t i = 0; i < countof (ints); i++)
    {
      lld = ints[i], ld = (long) lld, d = (int) ld;
      ret |= 0
        | SNPRINTF_CHECK ("%d%i", d, d)
        | SNPRINTF_CHECK ("%ld%li", ld, ld)
        | SNPRINTF_CHECK ("%lld%lli", lld, lld)
        | SNPRINTF_CHECK ("%d %ld %lld", d, ld, lld);
    }

  ret |= 0
    | SNPRINTF_CHECK ("%d%d%d", -1, 0, 1)
    | SNPRINTF_CHECK ("%d %d", INT_MIN, INT_MAX)
    | SNPRINTF_CHECK ("%lld %lld", LLONG_MIN, LLONG_MAX);

  return ret;
}

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
      ret |= 0
        | SNPRINTF_CHECK ("%u", u)
        | SNPRINTF_CHECK ("%lu", ul)
        | SNPRINTF_CHECK ("%llu", ull)
        | SNPRINTF_CHECK ("%u %lu %llu", u, ul, ull);
    }

  ret |= 0
    | SNPRINTF_CHECK ("%u %lu", UINT_MAX, ULONG_MAX)
    | SNPRINTF_CHECK ("%lu %llu", ULONG_MAX, ULLONG_MAX)
    | SNPRINTF_CHECK ("%u %lu %llu", UINT_MAX, ULONG_MAX, ULLONG_MAX);
  return ret;
}

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
      ret |= 0
        | SNPRINTF_CHECK ("%x", u)
        | SNPRINTF_CHECK ("%lx", ul)
        | SNPRINTF_CHECK ("%llx", ull)
        | SNPRINTF_CHECK ("%x%X", u, u)
        | SNPRINTF_CHECK ("%llX", ull)
        | SNPRINTF_CHECK ("%llx", ull >> 16)
        | SNPRINTF_CHECK ("%x %lx %llx", u, ul, ull)
        | SNPRINTF_CHECK ("%X %lX %llX", u, ul, ull);
    }

  ret |= 0
    | SNPRINTF_CHECK ("%x %X", UINT_MAX, UINT_MAX)
    | SNPRINTF_CHECK ("%lx %lX", ULONG_MAX, ULONG_MAX)
    | SNPRINTF_CHECK ("%llx %llX", ULLONG_MAX, ULLONG_MAX)
    | SNPRINTF_CHECK ("%x %lx %llx", UINT_MAX, ULONG_MAX, ULLONG_MAX);
  return ret;
}

static int
test_xnd_snprintf_ptr (void)
{
  int ret = 0;
  void *p1, *p2;

  for (size_t i = 0; i < countof (ptrs); i++)
    {
      p1 = ptrs[i], p2 = ptrs[countof (ptrs) - 1 - i];
      ret |= 0
        | SNPRINTF_CHECK ("%p", p1)
        | SNPRINTF_CHECK ("%p%p", p1, p2)
        | SNPRINTF_CHECK ("%p%%p", p1)
        | SNPRINTF_CHECK ("%p%%%p", p1, p2)
        | SNPRINTF_CHECK ("%p-%p", p1, p2);
    }

  return ret;
}

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
      ret |= 0
        | SNPRINTF_CHECK ("(+ (- %u %d) %lld)\n", _7.u, _4.d, _6.lld)
        | SNPRINTF_CHECK ("%c-%c %p %llu\n", _1.c, _1.c, _3.p, _9.llu)
        | SNPRINTF_CHECK ("%s (%p)\t\t %d/%c\n", _2.s, _3.p, _4.d, _1.c)
        | SNPRINTF_CHECK ("[%ld..%lu]\n", _5.ld, _8.lu);
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
      ret = SUBTEST_CHECK (&tests[i]);
      cnt += !!(ret == 0);
    }

  return (cnt < total ? -1 : 0);
}

static int
do_test (void)
{
  return test_xnd_snprintf ();
}

#define TEST_FUNCTION do_test
#include "tst-support.c"
