/* thread-port.c */
#include <mach/mach.h>
#include <pthread.h>

#include "xnd/xnd.h"
#include "common/atomic.h"
#include "common/compiler.h"
#include "common/xalloc.h"
#include "common/xthread.h"
#include "thread-port.h"

static inline uint32_t lowbias32 (uint32_t);
static inline uint32_t tport_tbl_index (mach_port_t);

/* Translation table mapping userspace thread ports to the thread port
   known by the kernel; After a restart, __TSD_MACH_THREAD_SELF is set
   to the original value, so the thread port appears to be unchanged in
   userspace. The translation table allows for translating this port value
   (uport) to the real thread port (kport). */
static struct tport_tbl tp_tbl =
  {
    .tp_entries = NULL,
    .tp_size = 0,
  };

static inline uint32_t
lowbias32 (uint32_t x)
{
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static inline uint32_t
tport_tbl_index (mach_port_t uport)
{
  uint32_t x = (uint32_t) uport;
  return (lowbias32 (x) % tp_tbl.tp_size);
}

void
tport_tbl_init (size_t size)
{
  const size_t n = size * 2;

  /* Allocate space for 2 * size slots in order to leave unused slots
     in the table. This provides on optimization for thread port
     translation, as finding a zero entry indicates that the thread port
     is not being virtualized (uport == kport). */
  tp_tbl.tp_size = n;
  tp_tbl.tp_entries = xrealloc (tp_tbl.tp_entries, n * sizeof (uint64_t));
  memset (tp_tbl.tp_entries, 0, n * sizeof (uint64_t));
}

void
tport_tbl_free (void)
{
  if (tp_tbl.tp_size != 0)
    {
      tp_tbl.tp_size = 0;
      free (exchange (tp_tbl.tp_entries, NULL));
    }
}

int
tport_tbl_insert (mach_port_t uport, mach_port_t kport)
{
  size_t index;
  uint64_t val, *slot;

  xnd_assert (tp_tbl.tp_size != 0);

  /* The caller is reponsible for ensuring that uport has not already
     been inserted into the table, otherwise, a new entry may be
     occupied by the same key. tport_tbl_insert does not handle this.

     Additonally, if the table was initialized to allocate space for
     N usable slots, tport_tbl_init will allocate 2N slots, so
     inserting past N is a bug that should be handled by the caller
     (see comment in tport_tbl_init). */
  if (MACH_PORT_VALID (uport) && MACH_PORT_VALID (kport))
    {
      index = tport_tbl_index (uport);
      val = tport_make_entry (uport, kport);
      for (size_t i = 0; i < tp_tbl.tp_size; i++)
        {
          slot = tp_tbl.tp_entries + index;
          if (xnd_atomic_cmpxchg_release (slot, 0, val))
            return 0;
          index = ((index + 1) % tp_tbl.tp_size);
        }
    }

  return -1;
}

int
tport_tbl_delete (mach_port_t uport)
{
  size_t index;
  uint64_t val, *slot;

  xnd_assert (tp_tbl.tp_size != 0);
  if (MACH_PORT_VALID (uport))
    {
      index = tport_tbl_index (uport);
      for (size_t i = 0; i < tp_tbl.tp_size; i++)
        {
          slot = tp_tbl.tp_entries + index;
          val = xnd_atomic_load_relaxed (slot);
          if (tport_entry_uport (val) == uport)
            {
              /* Mark the kport as MACH_PORT_DEAD upon deletion. The
                 entry still exists, so tport_tbl_user_to_kern will
                 return MACH_PORT_DEAD to indicate that the thread has
                 exited. */
              val = tport_make_entry (uport, MACH_PORT_DEAD);
              xnd_atomic_store_release (slot, val);
              return 0;
            }
          index = ((index + 1) % tp_tbl.tp_size);
        }
    }

  return -1;
}

/* tport_tbl_user_to_kern returns three possible values:
   MACH_PORT_NULL:
     If a thread was created after restart, it will not have a thread
     port translation in the table because the userspace thread port and
     the kernel thread port are the same. i.e. uport does not need to be
     virtualized.
   MACH_PORT_DEAD:
     A thread had a translation in the table, but has since exited.
   kport:
     A valid translation was found; kport is the thread port known by the
     kernel of the thread whose userspace port is uport. */
mach_port_t
tport_tbl_user_to_kern (mach_port_t uport)
{
  size_t index;
  uint64_t val, *slot;
  mach_port_t kport = MACH_PORT_NULL;

  if (MACH_PORT_VALID (uport) && tp_tbl.tp_size != 0)
    {
      index = tport_tbl_index (uport);
      for (size_t i = 0; i < tp_tbl.tp_size; i++)
        {
          slot = tp_tbl.tp_entries + index;
          val = xnd_atomic_load_acquire (slot);
          if (tport_entry_uport (val) == uport)
            {
              kport = tport_entry_kport (val);
              break;
            }
          else if (val == 0)
            /* see comment in tport_tbl_init */
            break;
          index = ((index + 1) % tp_tbl.tp_size);
        }
    }

  return kport;
}

mach_port_t
tport_tbl_kern_to_user (mach_port_t kport)
{
  uint64_t val, *slot;
  mach_port_t uport = MACH_PORT_NULL;

  if (MACH_PORT_VALID (kport) && tp_tbl.tp_size != 0)
    {
      for (size_t i = 0; i < tp_tbl.tp_size; i++)
        {
          slot = tp_tbl.tp_entries + i;
          val = xnd_atomic_load_acquire (slot);
          if (tport_entry_kport (val) == kport)
            {
              uport = tport_entry_uport (val);
              break;
            }
        }
    }

  return uport;
}
