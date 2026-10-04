/* ulock-defs.h */
#ifndef ULOCK_DEFS_H
#define ULOCK_DEFS_H

#include <stdint.h>

#define UL_COMPARE_AND_WAIT            1
#define UL_UNFAIR_LOCK                 2
#define UL_COMPARE_AND_WAIT_SHARED     3
#define UL_UNFAIR_LOCK64_SHARED        4
#define UL_COMPARE_AND_WAIT64          5
#define UL_COMPARE_AND_WAIT64_SHARED   6
#define UL_OSSPINLOCK                  UL_COMPARE_AND_WAIT
#define UL_HANDOFFLOCK                 UL_UNFAIR_LOCK
#define UL_DEBUG_SIMULATE_COPYIN_FAULT 253
#define UL_DEBUG_HASH_DUMP_ALL         254
#define UL_DEBUG_HASH_DUMP_PID         255

#define ULF_WAIT_WORKQ_DATA_CONTENTION 0x00010000
#define ULF_WAIT_CANCEL_POINT          0x00020000
#define ULF_WAIT_ADAPTIVE_SPIN         0x00040000

#define ULF_WAKE_ALL             0x00000100
#define ULF_WAKE_THREAD          0x00000200
#define ULF_WAKE_ALLOW_NON_OWNER 0x00000400

#define ULF_NO_ERRNO 0x01000000
#define ULF_DEADLINE 0x02000000

#define UL_OPCODE_MASK   0x000000FF
#define UL_FLAGS_MASK    0xFFFFFF00
#define ULF_GENERIC_MASK 0xFFFF0000

#define ULF_WAIT_MASK                                           \
  (ULF_NO_ERRNO | ULF_DEADLINE | ULF_WAIT_WORKQ_DATA_CONTENTION \
   | ULF_WAIT_CANCEL_POINT | ULF_WAIT_ADAPTIVE_SPIN)

#define ULF_WAKE_MASK \
  (ULF_NO_ERRNO | ULF_WAKE_ALL | ULF_WAKE_THREAD | ULF_WAKE_ALLOW_NON_OWNER)

extern int __ulock_wait (uint32_t op, void *addr, uint64_t val,
                         uint32_t timeout);
extern int __ulock_wait2 (uint32_t op, void *addr, uint64_t val,
                          uint64_t timeout, uint64_t val2);
extern int __ulock_wake (uint32_t op, void *addr, uint64_t val);

#endif /* ULOCK_DEFS_H */
