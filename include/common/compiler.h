/* compiler.h */
#ifndef XND_COMPILER_H
#define XND_COMPILER_H

#include <sys/cdefs.h>
#if __has_include(<os/base.h>)
#include <os/base.h>
#endif

#if __STDC_VERSION__ < 202311L
#include <stdbool.h>
#endif

#ifndef asm
#define asm __asm__
#endif

#ifndef typeof
#define typeof __typeof
#endif

#ifndef __has_include
#define __has_include(x) (0)
#endif
#ifndef __has_builtin
#define __has_builtin(x) (0)
#endif
#ifndef __has_attribute
#define __has_attribute(x) (0)
#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif

#ifndef __cplusplus
#define min(x, y)          \
  ({                       \
    typeof (x) __x = (x);  \
    typeof (y) __y = (y);  \
    (void)(&__x == &__y);  \
    __x < __y ? __x : __y; \
  })
#define max(x, y)          \
  ({                       \
    typeof (x) __x = (x);  \
    typeof (y) __y = (y);  \
    (void)(&__x == &__y);  \
    __x > __y ? __x : __y; \
  })
#endif

#define min_t(type, x, y)  \
  ({                       \
    type __x = (x);        \
    type __y = (y);        \
    __x < __y ? __x : __y; \
  })

#define max_t(type, x, y)  \
  ({                       \
    type __x = (x);        \
    type __y = (y);        \
    __x > __y ? __x : __y; \
  })

#ifndef __cplusplus
#define swap(x, y)            \
  do                          \
    {                         \
      typeof (x) __tmp = (x); \
      (x) = (y);              \
      (y) = __tmp;            \
    }                         \
  while (0)
#define exchange(x, y)      \
  ({                        \
    typeof (x) __tmp = (x); \
    (x) = (y);              \
    __tmp;                  \
  })
#endif

#define countof(x) (sizeof (x) / sizeof ((x)[0]))

#undef __CONCAT
#undef CONCAT
#define __CONCAT(a, b) a##b
#define CONCAT(a, b)   __CONCAT (a, b)

#define __STRINGIFY(s) #s
#define STRINGIFY(s)   __STRINGIFY (s)
#define TOSTRING(s)    __STRINGIFY (s)

/*
 * Always xnd-owned: never aliased to a `likely`/`unlikely` that a
 * vendored header may already have defined (macos/libdispatch defines
 * its own, and its non-GNUC fallback is a no-op).  Prefer these in xnd
 * code; the unprefixed names below are compatibility only.
 */
#define __xnd_likely(x)   __builtin_expect (!!(x), 1)
#define __xnd_unlikely(x) __builtin_expect (!!(x), 0)

#ifndef likely
#define likely(x) __xnd_likely (x)
#endif
#ifndef unlikely
#define unlikely(x) __xnd_unlikely (x)
#endif

#define READ_ONCE(var)       (*((volatile typeof (var) *)(&(var))))
#define WRITE_ONCE(var, val) (*((volatile typeof (var) *)(&(var))) = (val))

#ifndef barrier
#define barrier() __asm__ __volatile__ ("" ::: "memory")
#endif

#ifndef unreachable
#define unreachable() __builtin_unreachable ()
#endif

#ifndef __noreturn
#define __noreturn __attribute__ ((noreturn))
#endif

#ifndef __naked
#define __naked __attribute__ ((naked))
#endif

#define __ATTRIBUTE_0(name)     __attribute__ ((name))
#define __ATTRIBUTE_1(name, a1) __attribute__ ((name (a1)))

#define __ATTRIBUTE_NARGS_X(a, b, c, d, n, ...) n
#define __ATTRIBUTE_NARGS(...)                  __ATTRIBUTE_NARGS_X (__VA_ARGS__, 3, 2, 1, 0, )

#define __ATTRIBUTE_DISP(a, ...) \
  CONCAT (a, __ATTRIBUTE_NARGS (__VA_ARGS__)) (__VA_ARGS__)
#define __ATTRIBUTE_DECL(name, ...) \
  __ATTRIBUTE_DISP (__ATTRIBUTE_, name, ##__VA_ARGS__)

#define __constructor(...) __ATTRIBUTE_DECL (constructor, ##__VA_ARGS__)
#define __destructor(...)  __ATTRIBUTE_DECL (destructor, ##__VA_ARGS__)
#define __section(...)     __ATTRIBUTE_DECL (section, ##__VA_ARGS__)

#ifndef noinline
#define noinline __attribute__ ((noinline))
#endif

#ifndef __always_inline
#define __always_inline inline __attribute__ ((always_inline))
#endif

#ifndef __used
#define __used __attribute__ ((used))
#endif

#ifndef __unused
#define __unused __attribute__ ((unused))
#endif

#ifndef __aligned
#define __aligned(x) __attribute__ ((aligned (x)))
#endif

#ifndef __pure
#define __pure __attribute__ ((pure))
#endif

#ifndef __attribute_const__
#define __attribute_const__ __attribute__ ((const))
#endif

#ifndef __cold
#define __cold __attribute__ ((cold))
#endif

#ifndef __private_extern
#define __private_extern __attribute__ ((visibility ("hidden")))
#endif

#ifndef __no_stack_protector
#define __no_stack_protector __attribute__ ((no_stack_protector))
#endif

#ifndef fallthrough
#if __has_attribute(__fallthrough__)
#define fallthrough __attribute__ ((__fallthrough__))
#else
#define fallthrough \
  do                \
    {               \
    }               \
  while (0)
#endif
#endif

#ifndef __warn_unused
#define __warn_unused __attribute__ ((warn_unused_result))
#endif

/* Ignores qualifiers */
#define xnd_same_type(a, b) \
  __builtin_types_compatible_p (typeof (a), typeof (b))

/* pointer_type_class = 5, array_type_class = 14 */
#ifndef pointer_type_class
#define pointer_type_class 5
#endif
#ifndef array_type_class
#define array_type_class 14
#endif

#define xnd_is_array_or_pointer(x)                   \
  (__builtin_classify_type (x) == pointer_type_class \
   || __builtin_classify_type (x) == array_type_class)

#define xnd_is_pointer(p) \
  (xnd_is_array_or_pointer (p) && xnd_same_type ((p), &(p)[0]))
#define xnd_is_array(a) \
        (xnd_is_array_or_pointer(a) && !xnd_same_type((a), &(a)[0]))
#define xnd_must_be_array(a) \
  static_assert (xnd_is_array (a), STRINGIFY (a) " is not an array")

#define xnd_is_signed(x)   ((__typeof__ (x))(-1) < (__typeof__ (x))(1))
#define xnd_is_unsigned(x) (!xnd_is_signed (x))

static inline bool
xnd_warn_unused (const bool x)
{
  return x;
}

#define xnd_add_overflow(x, y, res) \
  xnd_warn_unused (__builtin_add_overflow ((x), (y), (res)))
#define xnd_sub_overflow(x, y, res) \
  xnd_warn_unused (__builtin_sub_overflow ((x), (y), (res)))
#define xnd_mul_overflow(x, y, res) \
  xnd_warn_unused (__builtin_mul_overflow ((x), (y), (res)))

#if __has_attribute(__optimize__)
#define __optimize(level) __attribute__ ((__optimize__ (level)))
#else
#define __optimize(level)
#endif

#define __attribute_nonnull__(...) __attribute__ ((__nonnull__ (__VA_ARGS__)))

#endif /* XND_COMPILER_H */
