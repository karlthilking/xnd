/* atomic.h */
#ifndef XND_ATOMIC_H
#define XND_ATOMIC_H

#ifndef __cplusplus /* Not adopted for c++ */

#include <stdatomic.h>
#include <os/atomic.h>
#include <mach/thread_switch.h>

#include "compiler.h"

#define xnd_atomic(type)      _Atomic volatile type
#define xnd_atomic_typeof(x)  xnd_atomic (__typeof__ (x))
#define xnd_cast_to_atomic(p) ((xnd_atomic_typeof (*(p)) *)(p))

#define xnd_unqual_typeof(x) \
  __typeof__ (atomic_load ((_Atomic volatile __typeof__ (x) *)0))
#define xnd_cast_to_unqual(x) ((xnd_unqual_typeof (x)) (x))

#define xnd_atomic_value_cast(p, v)     \
  ({                                    \
    xnd_unqual_typeof (*(p)) __v = (v); \
    __v;                                \
  })

extern kern_return_t thread_switch (mach_port_t, int, mach_msg_timeout_t);

#define xnd_atomic_yield() thread_switch (THREAD_NULL, SWITCH_OPTION_NONE, 0)

static_assert (memory_order_relaxed == __ATOMIC_RELAXED, "");
static_assert (memory_order_acquire == __ATOMIC_ACQUIRE, "");
static_assert (memory_order_release == __ATOMIC_RELEASE, "");
static_assert (memory_order_acq_rel == __ATOMIC_ACQ_REL, "");
static_assert (memory_order_seq_cst == __ATOMIC_SEQ_CST, "");

#define xnd_release_barrier_relaxed memory_order_relaxed
#define xnd_release_barrier_acquire memory_order_relaxed
#define xnd_release_barrier_release memory_order_release
#define xnd_release_barrier_acq_rel memory_order_release
#define xnd_release_barrier_seq_cst memory_order_release

#define xnd_acquire_barrier_relaxed memory_order_relaxed
#define xnd_acquire_barrier_acquire memory_order_acquire
#define xnd_acquire_barrier_release memory_order_relaxed
#define xnd_acquire_barrier_acq_rel memory_order_acquire
#define xnd_acquire_barrier_seq_cst memory_order_acquire

#define xnd_compiler_barrier_before_atomic(m) \
  atomic_signal_fence (xnd_release_barrier_##m)
#define xnd_compiler_barrier_after_atomic(m) \
  atomic_signal_fence (xnd_acquire_barrier_##m)

#define xnd_compiler_barrier_acquire() \
  atomic_signal_fence (memory_order_acquire)
#define xnd_compiler_barrier_release() \
  atomic_signal_fence (memory_order_release)
#define xnd_compiler_barrier_acq_rel() \
  atomic_signal_fence (memory_order_acq_rel)
#define xnd_compiler_barrier_seq_cst() \
  atomic_signal_fence (memory_order_seq_cst)

#define xnd_memory_fence_acquire() atomic_thread_fence (memory_order_acquire)
#define xnd_memory_fence_release() atomic_thread_fence (memory_order_release)
#define xnd_memory_fence_acq_rel() atomic_thread_fence (memory_order_acq_rel)
#define xnd_memory_fence_seq_cst() atomic_thread_fence (memory_order_seq_cst)

#define xnd_atomic_load(p, m)                                            \
  ({                                                                     \
    xnd_compiler_barrier_before_atomic (m);                              \
    __auto_type __v =                                                    \
        atomic_load_explicit (xnd_cast_to_atomic (p), memory_order_##m); \
    xnd_compiler_barrier_after_atomic (m);                               \
    __v;                                                                 \
  })

#define xnd_atomic_load_relaxed(p) xnd_atomic_load (p, relaxed)
#define xnd_atomic_load_acquire(p) xnd_atomic_load (p, acquire)
#define xnd_atomic_load_seq_cst(p) xnd_atomic_load (p, seq_cst)

#define xnd_atomic_store(p, v, m)                                          \
  ((void)({                                                                \
    __auto_type __v = (v);                                                 \
    xnd_compiler_barrier_before_atomic (m);                                \
    atomic_store_explicit (xnd_cast_to_atomic (p), __v, memory_order_##m); \
    xnd_compiler_barrier_after_atomic (m);                                 \
  }))

#define xnd_atomic_store_relaxed(p, v) xnd_atomic_store (p, v, relaxed)
#define xnd_atomic_store_release(p, v) xnd_atomic_store (p, v, release)
#define xnd_atomic_store_seq_cst(p, v) xnd_atomic_store (p, v, seq_cst)

#define xnd_atomic_xchg(p, v, m)                                              \
  ({                                                                          \
    xnd_compiler_barrier_before_atomic (m);                                   \
    __auto_type __r = atomic_exchange_explicit (xnd_cast_to_atomic (p),       \
                                                xnd_atomic_value_cast (p, v), \
                                                memory_order_##m);            \
    xnd_compiler_barrier_after_atomic (m);                                    \
    __r;                                                                      \
  })

#define xnd_atomic_xchg_relaxed(p, v) xnd_atomic_xchg (p, v, relaxed)
#define xnd_atomic_xchg_acquire(p, v) xnd_atomic_xchg (p, v, acquire)
#define xnd_atomic_xchg_release(p, v) xnd_atomic_xchg (p, v, release)
#define xnd_atomic_xchg_acq_rel(p, v) xnd_atomic_xchg (p, v, acq_rel)
#define xnd_atomic_xchg_seq_cst(p, v) xnd_atomic_xchg (p, v, seq_cst)

#define xnd_atomic_cmpxchg(p, e, v, m, type)                        \
  ({                                                                \
    __auto_type __e = xnd_atomic_value_cast (p, e);                 \
    xnd_compiler_barrier_before_atomic (m);                         \
    __auto_type __r = atomic_compare_exchange_##type##_explicit (   \
        xnd_cast_to_atomic (p), &__e, xnd_atomic_value_cast (p, v), \
        memory_order_##m, memory_order_relaxed);                    \
    xnd_compiler_barrier_after_atomic (m);                          \
    __r;                                                            \
  })

#define xnd_atomic_cmpxchg_relaxed(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, relaxed, strong)
#define xnd_atomic_cmpxchg_acquire(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, acquire, strong)
#define xnd_atomic_cmpxchg_release(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, release, strong)
#define xnd_atomic_cmpxchg_acq_rel(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, acq_rel, strong)
#define xnd_atomic_cmpxchg_seq_cst(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, seq_cst, strong)

#define xnd_atomic_cmpxchg_weak_relaxed(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, relaxed, weak)
#define xnd_atomic_cmpxchg_weak_acquire(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, acquire, weak)
#define xnd_atomic_cmpxchg_weak_release(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, release, weak)
#define xnd_atomic_cmpxchg_weak_acq_rel(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, acq_rel, weak)
#define xnd_atomic_cmpxchg_weak_seq_cst(p, e, v) \
  xnd_atomic_cmpxchg (p, e, v, seq_cst, weak)

#define xnd_atomic_cmpxchgv(p, e, v, m, type)                      \
  ({                                                               \
    __auto_type __e = (xnd_unqual_typeof (p)) (e);                 \
    xnd_compiler_barrier_before_atomic (m);                        \
    __auto_type __r = atomic_compare_exchange_##type##_explicit (  \
        xnd_cast_to_atomic (p), __e, xnd_atomic_value_cast (p, v), \
        memory_order_##m, memory_order_relaxed);                   \
    xnd_compiler_barrier_after_atomic (m);                         \
    __r;                                                           \
  })

#define xnd_atomic_cmpxchgv_relaxed(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, relaxed, strong)
#define xnd_atomic_cmpxchgv_acquire(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, acquire, strong)
#define xnd_atomic_cmpxchgv_release(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, release, strong)
#define xnd_atomic_cmpxchgv_acq_rel(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, acq_rel, strong)
#define xnd_atomic_cmpxchgv_seq_cst(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, seq_cst, strong)

#define xnd_atomic_cmpxchgv_weak_relaxed(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, relaxed, weak)
#define xnd_atomic_cmpxchgv_weak_acquire(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, acquire, weak)
#define xnd_atomic_cmpxchgv_weak_release(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, release, weak)
#define xnd_atomic_cmpxchgv_weak_acq_rel(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, acq_rel, weak)
#define xnd_atomic_cmpxchgv_weak_seq_cst(p, e, v) \
  xnd_atomic_cmpxchgv (p, e, v, seq_cst, weak)

#define xnd_atomic_c11_fetch_op(o, p, v, m)                                \
  ({                                                                       \
    xnd_compiler_barrier_before_atomic (m);                                \
    __auto_type __r = atomic_##o##_explicit (xnd_cast_to_atomic (p),       \
                                             xnd_atomic_value_cast (p, v), \
                                             memory_order_##m);            \
    xnd_compiler_barrier_after_atomic (m);                                 \
    __r;                                                                   \
  })

#define xnd_atomic_c11_op(o, p, v, m, op)                   \
  ({                                                        \
    __auto_type __v = xnd_atomic_value_cast (p, v);         \
    __auto_type __r = xnd_atomic_c11_fetch_op (o, p, v, m); \
    op (__r, __v);                                          \
  })

#define xnd_atomic_builtin_fetch_op(o, p, v, m)                            \
  ({                                                                       \
    xnd_compiler_barrier_before_atomic (m);                                \
    __auto_type __r =                                                      \
        __atomic_##o (xnd_cast_to_unqual (p),                              \
                      xnd_atomic_value_cast ((p), (v)), memory_order_##m); \
    xnd_compiler_barrier_after_atomic (m);                                 \
    __r;                                                                   \
  })

#define xnd_atomic_builtin_op(o, p, v, m, op)                   \
  ({                                                            \
    __auto_type __v = xnd_atomic_value_cast (p, v);             \
    __auto_type __r = xnd_atomic_builtin_fetch_op (o, p, v, m); \
    op (__r, __v);                                              \
  })

#define xnd_binary_op(x, y, op) \
  ({                            \
    __typeof__ (x) __x = (x);   \
    __typeof__ (y) __y = (y);   \
    __x op __y;                 \
  })

#define xnd_add(x, y) xnd_binary_op (x, y, +)
#define xnd_sub(x, y) xnd_binary_op (x, y, -)
#define xnd_and(x, y) xnd_binary_op (x, y, &)
#define xnd_or(x, y)  xnd_binary_op (x, y, |)
#define xnd_xor(x, y) xnd_binary_op (x, y, ^)

#define xnd_atomic_inc(p, m) xnd_atomic_c11_op (fetch_add, p, 1, m, xnd_add)

#define xnd_atomic_inc_relaxed(p) xnd_atomic_inc (p, relaxed)
#define xnd_atomic_inc_acquire(p) xnd_atomic_inc (p, acquire)
#define xnd_atomic_inc_release(p) xnd_atomic_inc (p, release)
#define xnd_atomic_inc_acq_rel(p) xnd_atomic_inc (p, acq_rel)
#define xnd_atomic_inc_seq_cst(p) xnd_atomic_inc (p, seq_cst)

#define xnd_atomic_fetch_inc(p, m) xnd_atomic_c11_fetch_op (fetch_add, p, 1, m)

#define xnd_atomic_fetch_inc_relaxed(p) xnd_atomic_fetch_inc (p, relaxed)
#define xnd_atomic_fetch_inc_acquire(p) xnd_atomic_fetch_inc (p, acquire)
#define xnd_atomic_fetch_inc_release(p) xnd_atomic_fetch_inc (p, release)
#define xnd_atomic_fetch_inc_acq_rel(p) xnd_atomic_fetch_inc (p, acq_rel)
#define xnd_atomic_fetch_inc_seq_cst(p) xnd_atomic_fetch_inc (p, seq_cst)

#define xnd_atomic_dec(p, m) xnd_atomic_c11_op (fetch_sub, p, 1, m, xnd_sub)

#define xnd_atomic_dec_relaxed(p) xnd_atomic_dec (p, relaxed)
#define xnd_atomic_dec_acquire(p) xnd_atomic_dec (p, acquire)
#define xnd_atomic_dec_release(p) xnd_atomic_dec (p, release)
#define xnd_atomic_dec_acq_rel(p) xnd_atomic_dec (p, acq_rel)
#define xnd_atomic_dec_seq_cst(p) xnd_atomic_dec (p, seq_cst)

#define xnd_atomic_fetch_dec(p, m) xnd_atomic_c11_fetch_op (fetch_sub, p, 1, m)

#define xnd_atomic_fetch_dec_relaxed(p) xnd_atomic_fetch_dec (p, relaxed)
#define xnd_atomic_fetch_dec_acquire(p) xnd_atomic_fetch_dec (p, acquire)
#define xnd_atomic_fetch_dec_release(p) xnd_atomic_fetch_dec (p, release)
#define xnd_atomic_fetch_dec_acq_rel(p) xnd_atomic_fetch_dec (p, acq_rel)
#define xnd_atomic_fetch_dec_seq_cst(p) xnd_atomic_fetch_dec (p, seq_cst)

#define xnd_atomic_add(p, v, m) xnd_atomic_c11_op (fetch_add, p, v, m, xnd_add)

#define xnd_atomic_add_relaxed(p) xnd_atomic_add (p, relaxed)
#define xnd_atomic_add_acquire(p) xnd_atomic_add (p, acquire)
#define xnd_atomic_add_release(p) xnd_atomic_add (p, release)
#define xnd_atomic_add_acq_rel(p) xnd_atomic_add (p, acq_rel)
#define xnd_atomic_add_seq_cst(p) xnd_atomic_add (p, seq_cst)

#define xnd_atomic_fetch_add(p, v, m) \
  xnd_atomic_c11_fetch_op (fetch_add, p, v, m)

#define xnd_atomic_fetch_add_relaxed(p) xnd_atomic_fetch_add (p, relaxed)
#define xnd_atomic_fetch_add_acquire(p) xnd_atomic_fetch_add (p, acquire)
#define xnd_atomic_fetch_add_release(p) xnd_atomic_fetch_add (p, release)
#define xnd_atomic_fetch_add_acq_rel(p) xnd_atomic_fetch_add (p, acq_rel)
#define xnd_atomic_fetch_add_seq_cst(p) xnd_atomic_fetch_add (p, seq_cst)

#define xnd_atomic_sub(p, v, m) xnd_atomic_c11_op (fetch_sub, p, v, m, xnd_sub)

#define xnd_atomic_sub_relaxed(p) xnd_atomic_sub (p, relaxed)
#define xnd_atomic_sub_acquire(p) xnd_atomic_sub (p, acquire)
#define xnd_atomic_sub_release(p) xnd_atomic_sub (p, release)
#define xnd_atomic_sub_acq_rel(p) xnd_atomic_sub (p, acq_rel)
#define xnd_atomic_sub_seq_cst(p) xnd_atomic_sub (p, seq_cst)

#define xnd_atomic_fetch_sub(p, v, m) \
  xnd_atomic_c11_fetch_op (fetch_sub, p, v, m)

#define xnd_atomic_fetch_sub_relaxed(p) xnd_atomic_fetch_sub (p, relaxed)
#define xnd_atomic_fetch_sub_acquire(p) xnd_atomic_fetch_sub (p, acquire)
#define xnd_atomic_fetch_sub_release(p) xnd_atomic_fetch_sub (p, release)
#define xnd_atomic_fetch_sub_acq_rel(p) xnd_atomic_fetch_sub (p, acq_rel)
#define xnd_atomic_fetch_sub_seq_cst(p) xnd_atomic_fetch_sub (p, seq_cst)

#define xnd_atomic_and(p, v, m) xnd_atomic_c11_op (fetch_and, p, v, m, xnd_and)

#define xnd_atomic_and_relaxed(p) xnd_atomic_and (p, relaxed)
#define xnd_atomic_and_acquire(p) xnd_atomic_and (p, acquire)
#define xnd_atomic_and_release(p) xnd_atomic_and (p, release)
#define xnd_atomic_and_acq_rel(p) xnd_atomic_and (p, acq_rel)
#define xnd_atomic_and_seq_cst(p) xnd_atomic_and (p, seq_cst)

#define xnd_atomic_fetch_and(p, v, m) \
  xnd_atomic_c11_fetch_op (fetch_and, p, v, m)

#define xnd_atomic_fetch_and_relaxed(p) xnd_atomic_fetch_and (p, relaxed)
#define xnd_atomic_fetch_and_acquire(p) xnd_atomic_fetch_and (p, acquire)
#define xnd_atomic_fetch_and_release(p) xnd_atomic_fetch_and (p, release)
#define xnd_atomic_fetch_and_acq_rel(p) xnd_atomic_fetch_and (p, acq_rel)
#define xnd_atomic_fetch_and_seq_cst(p) xnd_atomic_fetch_and (p, seq_cst)

#define xnd_atomic_or(p, v, m) xnd_atomic_c11_op (fetch_or, p, v, m, xnd_or)

#define xnd_atomic_or_relaxed(p) xnd_atomic_or (p, relaxed)
#define xnd_atomic_or_acquire(p) xnd_atomic_or (p, acquire)
#define xnd_atomic_or_release(p) xnd_atomic_or (p, release)
#define xnd_atomic_or_acq_rel(p) xnd_atomic_or (p, acq_rel)
#define xnd_atomic_or_seq_cst(p) xnd_atomic_or (p, seq_cst)

#define xnd_atomic_fetch_or(p, v, m) \
  xnd_atomic_c11_fetch_op (fetch_or, p, v, m)

#define xnd_atomic_fetch_or_relaxed(p) xnd_atomic_fetch_or (p, relaxed)
#define xnd_atomic_fetch_or_acquire(p) xnd_atomic_fetch_or (p, acquire)
#define xnd_atomic_fetch_or_release(p) xnd_atomic_fetch_or (p, release)
#define xnd_atomic_fetch_or_acq_rel(p) xnd_atomic_fetch_or (p, acq_rel)
#define xnd_atomic_fetch_or_seq_cst(p) xnd_atomic_fetch_or (p, seq_cst)

#define xnd_atomic_xor(p, v, m) xnd_atomic_c11_op (fetch_xor, p, v, m, xnd_xor)

#define xnd_atomic_xor_relaxed(p) xnd_atomic_xor (p, relaxed)
#define xnd_atomic_xor_acquire(p) xnd_atomic_xor (p, acquire)
#define xnd_atomic_xor_release(p) xnd_atomic_xor (p, release)
#define xnd_atomic_xor_acq_rel(p) xnd_atomic_xor (p, acq_rel)
#define xnd_atomic_xor_seq_cst(p) xnd_atomic_xor (p, seq_cst)

#define xnd_atomic_fetch_xor(p, v, m) \
  xnd_atomic_c11_fetch_op (fetch_xor, p, v, m)

#define xnd_atomic_fetch_xor_relaxed(p) xnd_atomic_fetch_xor (p, relaxed)
#define xnd_atomic_fetch_xor_acquire(p) xnd_atomic_fetch_xor (p, acquire)
#define xnd_atomic_fetch_xor_release(p) xnd_atomic_fetch_xor (p, release)
#define xnd_atomic_fetch_xor_acq_rel(p) xnd_atomic_fetch_xor (p, acq_rel)
#define xnd_atomic_fetch_xor_seq_cst(p) xnd_atomic_fetch_xor (p, seq_cst)

#define xnd_atomic_max(p, v, m) xnd_atomic_bulitin_op (fetch_max, p, v, m, max)

#define xnd_atomic_max_relaxed(p) xnd_atomic_max (p, relaxed)
#define xnd_atomic_max_acquire(p) xnd_atomic_max (p, acquire)
#define xnd_atomic_max_release(p) xnd_atomic_max (p, release)
#define xnd_atomic_max_acq_rel(p) xnd_atomic_max (p, acq_rel)
#define xnd_atomic_max_seq_cst(p) xnd_atomic_max (p, seq_cst)

#define xnd_atomic_fetch_max(p, v, m) \
  xnd_atomic_builtin_fetch_op (fetch_max, p, v, m)

#define xnd_atomic_fetch_max_relaxed(p) xnd_atomic_fetch_max (p, relaxed)
#define xnd_atomic_fetch_max_acquire(p) xnd_atomic_fetch_max (p, acquire)
#define xnd_atomic_fetch_max_release(p) xnd_atomic_fetch_max (p, release)
#define xnd_atomic_fetch_max_acq_rel(p) xnd_atomic_fetch_max (p, acq_rel)
#define xnd_atomic_fetch_max_seq_cst(p) xnd_atomic_fetch_max (p, seq_cst)

#define xnd_atomic_min(p, v, m) xnd_atomic_bulitin_op (fetch_min, p, v, m, min)

#define xnd_atomic_min_relaxed(p) xnd_atomic_min (p, relaxed)
#define xnd_atomic_min_acquire(p) xnd_atomic_min (p, acquire)
#define xnd_atomic_min_release(p) xnd_atomic_min (p, release)
#define xnd_atomic_min_acq_rel(p) xnd_atomic_min (p, acq_rel)
#define xnd_atomic_min_seq_cst(p) xnd_atomic_min (p, seq_cst)

#define xnd_atomic_fetch_min(p, v, m) \
  xnd_atomic_bulitin_fetch_op (fetch_min, p, v, m)

#define xnd_atomic_fetch_min_relaxed(p) xnd_atomic_fetch_min (p, relaxed)
#define xnd_atomic_fetch_min_acquire(p) xnd_atomic_fetch_min (p, acquire)
#define xnd_atomic_fetch_min_release(p) xnd_atomic_fetch_min (p, release)
#define xnd_atomic_fetch_min_acq_rel(p) xnd_atomic_fetch_min (p, acq_rel)
#define xnd_atomic_fetch_min_seq_cst(p) xnd_atomic_fetch_min (p, seq_cst)

#endif /* !__cplusplus */
#endif /* XND_ATOMIC_H */
