/* xnd-spinlock.h */
#ifndef XND_SPINLOCK_H
#define XND_SPINLOCK_H

#include <mach/mach.h>
#include <mach/thread_switch.h>
#include "compiler.h"

extern kern_return_t thread_switch (mach_port_t, int, mach_msg_timeout_t);

typedef uint32_t xnd_spinlock_t;

#define xnd_spinlock_yield() thread_switch (THREAD_NULL, SWITCH_OPTION_NONE, 0)

#define xnd_spinlock_lock(sp)                                \
  ((void) ({                                                 \
    if (__xnd_unlikely (atomic_xchg_acquire ((sp), 1) != 0)) \
      {                                                      \
        do                                                   \
          {                                                  \
            do                                               \
              xnd_spinlock_yield ();                         \
            while (atomic_load_relaxed (sp) != 0);           \
          }                                                  \
        while (atomic_xchg_acquire ((sp), 1) != 0);          \
      }                                                      \
  }))

#define xnd_spinlock_unlock(sp)              \
  ((void) ({                                 \
    struct xnd_spinlock *__sp = (sp);        \
    atomic_store_release (&__sp->sp_val, 0); \
  }))

#endif /* XND_SPINLOCK_H */
