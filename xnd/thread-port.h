/* thread-port.h */
#ifndef XND_THREAD_PORT_H
#define XND_THREAD_PORT_H

#include <mach/mach.h>
#include <pthread.h>


struct tport_tbl
{
  uint64_t *tp_entries;
  size_t tp_size;
};

#define TPORT_UPORT_MASK (0xFFFFFFFF00000000ULL)
#define TPORT_KPORT_MASK (0x00000000FFFFFFFFULL)

#define tport_make_entry(u, k) \
  (((uint64_t) (u) << 32) | (uint64_t) (k))
#define tport_entry_uport(v) \
  (mach_port_t) ((uint64_t) (v) >> 32)
#define tport_entry_kport(v) \
  (mach_port_t) ((uint64_t) (v) & TPORT_KPORT_MASK)

__BEGIN_DECLS

/* tport_tbl_init should be called by the checkpoint thread during
   restart, before any user threads have been restarted. All
   subsequent inserts must happen before the restart barrier is
   released (user threads are resumed).

   tport_tbl_free should only ever be called by libxnd's destructor
   that runs during exit.

   In general, the thread port table should not be changed while user
   threads are running; init, free, and insert should not be called
   during usual runtime. tport_tbl_delete is the exception to this
   rule, and should be called when a thread is exiting. */

extern void tport_tbl_init (size_t);
extern void tport_tbl_free (void);

extern int tport_tbl_insert (mach_port_t, mach_port_t);
extern int tport_tbl_delete (mach_port_t);

extern mach_port_t tport_tbl_user_to_kern (mach_port_t);
extern mach_port_t tport_tbl_kern_to_user (mach_port_t);

__END_DECLS
#endif /* XND_THREAD_PORT_H */
