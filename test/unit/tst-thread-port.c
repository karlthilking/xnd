/* test-thread-port.c */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "common/xalloc.h"
#include "common/xthread.h"
#include "tst-support.h"
#include "thread-port.h"

struct thread_args
{
  mach_port_t uport;
  mach_port_t kport;
};

static mach_port_t
next_mach_port (void)
{
  static mach_port_t port = MACH_PORT_NULL;

  while ((port == MACH_PORT_NULL) || (port == MACH_PORT_DEAD))
    port++;

  return port++;
}

/* Upper bound on slots per requested entry; guards fill loops against
   a table that never reports being full */
#define TPORT_MAX_SLOTS_PER_ENTRY 16

/* Inserts fresh entries until tport_tbl_insert fails and returns the
   number inserted. If uports and kports are not NULL, they receive the
   inserted entries and must have room for max entries. Returns max + 1
   if the table accepted more than max entries. */
static size_t
tport_tbl_fill (size_t max, mach_port_t *uports, mach_port_t *kports)
{
  size_t n;
  mach_port_t uport, kport;

  for (n = 0; n <= max; n++)
    {
      uport = next_mach_port ();
      kport = next_mach_port ();
      if (tport_tbl_insert (uport, kport) != 0)
        break;
      if (n < max && uports != NULL)
        {
          uports[n] = uport;
          kports[n] = kport;
        }
    }

  return n;
}

/* Number of slots tport_tbl_init (n) allocates, measured by filling a
   table. Leaves the table initialized and full. */
static size_t
tport_tbl_capacity (size_t n)
{
  tport_tbl_init (n);
  return tport_tbl_fill (n * TPORT_MAX_SLOTS_PER_ENTRY, NULL, NULL);
}

/* Mirrors tport_tbl_index in thread-port.c so that tests can choose
   uports that share a home slot. The table does not expose slot
   positions, so if the two get out of sync, test_tport_tbl_probe_chain
   still passes but no longer constructs collisions. */
static size_t
home_slot (mach_port_t uport, size_t nslots)
{
  uint32_t x = (uint32_t) uport;

  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x % nslots;
}

/* Returns the next uport whose home slot is home */
static mach_port_t
next_mach_port_at (size_t home, size_t nslots)
{
  mach_port_t port;

  do
    port = next_mach_port ();
  while (home_slot (port, nslots) != home);

  return port;
}

static void *
thread_doit (void *arg)
{
  struct thread_args *args = (struct thread_args *) arg;

  /* +1 reference on thread port, freed by parent thread */
  args->kport = mach_thread_self ();

  /* Check thread port table insertion and translation */
  T_ASSERT (tport_tbl_insert (args->uport, args->kport) == 0);
  T_ASSERT (tport_tbl_user_to_kern (args->uport) == args->kport);
  T_ASSERT (tport_tbl_kern_to_user (args->kport) == args->uport);

  pthread_exit (NULL);
}

static int
test_tport_tbl_basic (void)
{
  mach_port_t task = mach_task_self ();
  __block mach_port_t uport, kport;
  int (^do_one_test) (size_t) = NULL;
  size_t table_sizes[] =
    {
      1, 2, 3, 4, 7, 8, 12, 15, 16, 20, 24, 27, 31, 32, 50, 64,
      75, 81, 92, 95, 100, 101, 125, 128, 200, 256, 302, 512, 750,
      1024, 1200, 1315, 1500, 1600, 1750, 2000, 2048, 2500, 4096,
    };

  /* Test that each insertion from 1..n is successful for any n, and that
     user-to-kern and kern-to-user translation return the right port. */
  do_one_test = ^(size_t n)
    {
      tport_tbl_init (n);
      for (size_t i = 0; i < n; i++)
        {
          uport = next_mach_port ();
          kport = next_mach_port ();

          /* Verify insertion and translation */
          T_CHECK (tport_tbl_insert (uport, kport) == 0);
          T_CHECK (tport_tbl_user_to_kern (uport) == kport);
          T_CHECK (tport_tbl_kern_to_user (kport) == uport);

          /* Verify deletion keeps entry for uport and sets kport
             to MACH_PORT_DEAD */
          T_CHECK (tport_tbl_delete (uport) == 0);
          T_CHECK (tport_tbl_user_to_kern (uport) == MACH_PORT_DEAD);
        }

      /* user_to_kern on a non-existent key and kern_to_user with a
         non-existent value should return MACH_PORT_NULL */
      uport = next_mach_port ();
      kport = next_mach_port ();
      T_CHECK (tport_tbl_user_to_kern (uport) == MACH_PORT_NULL);
      T_CHECK (tport_tbl_kern_to_user (kport) == MACH_PORT_NULL);

      tport_tbl_free ();
      return 0;
    };

  for (int i = 0; i < countof (table_sizes); i++)
    {
      if (do_one_test (table_sizes[i]) != 0)
        return 1;
    }

  uport = next_mach_port ();
  kport = mach_thread_self ();
  tport_tbl_init (1);

  T_CHECK (tport_tbl_insert (uport, kport) == 0);
  T_CHECK (tport_tbl_user_to_kern (uport) == kport);
  T_CHECK (tport_tbl_kern_to_user (kport) == uport);

  /* Check deletion behavior, a deleted entry should transform a previous
     entry (uport, kport) to (uport, MACH_PORT_DEAD), so user-to-kern
     should return MACH_PORT_DEAD */
  T_CHECK (tport_tbl_delete (uport) == 0);
  T_CHECK (tport_tbl_user_to_kern (uport) == MACH_PORT_DEAD);

  tport_tbl_free ();
  T_ASSERT (mach_port_deallocate (task, kport) == KERN_SUCCESS);

  return 0;
}

static int
test_tport_tbl_threaded (void)
{
  int (^spawn) (int) = NULL;
  __block pthread_t *threads = NULL;
  __block struct thread_args *args = NULL;
  const int thread_counts[] =
    {
      1, 2, 4, 5, 11, 13, 16, 31, 48, 64, 96, 100, 101, 112,
      128, 175, 200, 256, 258, 300, 305, 400, 425, 512, 1024,
    };

  spawn = ^(int nthreads)
    {
      mach_port_t uport, kport, task = mach_task_self ();

      tport_tbl_init (nthreads);
      args = xmalloc (nthreads * sizeof (*args));
      threads = xmalloc (nthreads * sizeof (*threads));

      for (int i = 0; i < nthreads; i++)
        {
          args[i].uport = next_mach_port ();
          xpthread_create (&threads[i], NULL, thread_doit, &args[i]);
        }

      for (int i = 0; i < nthreads; i++)
        {
          xpthread_join (threads[i], NULL);
          uport = args[i].uport;
          kport = args[i].kport;

          /* Check that table correctly translates thread ports going
             both directions (user -> kern, kern -> user) */
          T_CHECK (tport_tbl_user_to_kern (uport) == kport);
          T_CHECK (tport_tbl_kern_to_user (kport) == uport);

          /* Delete entry from table; a translation attempt after deletion
             should return value = MACH_PORT_DEAD */
          T_CHECK (tport_tbl_delete (uport) == 0);
          T_CHECK (tport_tbl_user_to_kern (uport) == MACH_PORT_DEAD);
        }

      /* Release the references taken in thread_doit only after every
         entry has been deleted */
      for (int i = 0; i < nthreads; i++)
        {
          kport = args[i].kport;
          T_CHECK (mach_port_deallocate (task, kport) == KERN_SUCCESS);
        }

      tport_tbl_free ();
      free (args);
      free (threads);
      return 0;
    };

  for (int i = 0; i < countof (thread_counts); i++)
    {
      if (spawn (thread_counts[i]) != 0)
        return 1;
    }

  return 0;
}

/* Before the first tport_tbl_init (a process that has never been
   restarted) and after tport_tbl_free, the table is empty: lookups
   return MACH_PORT_NULL without indexing into it. Must run before any
   other subtest calls tport_tbl_init. */
static int
test_tport_tbl_uninit (void)
{
  mach_port_t uport = next_mach_port (), kport = next_mach_port ();

  T_CHECK (tport_tbl_user_to_kern (uport) == MACH_PORT_NULL);
  T_CHECK (tport_tbl_kern_to_user (kport) == MACH_PORT_NULL);

  tport_tbl_init (4);
  T_CHECK (tport_tbl_insert (uport, kport) == 0);
  T_CHECK (tport_tbl_user_to_kern (uport) == kport);
  T_CHECK (tport_tbl_kern_to_user (kport) == uport);
  tport_tbl_free ();

  T_CHECK (tport_tbl_user_to_kern (uport) == MACH_PORT_NULL);
  T_CHECK (tport_tbl_kern_to_user (kport) == MACH_PORT_NULL);

  /* tport_tbl_free on a freed table is a no-op */
  tport_tbl_free ();
  T_CHECK (tport_tbl_user_to_kern (uport) == MACH_PORT_NULL);

  return 0;
}

/* Each restart after the first calls tport_tbl_init on the previous
   checkpoint's table without freeing it. No entries from the previous
   table may survive, whether the table shrinks, keeps its size, or
   grows, and the new table has all of its slots available. */
static int
test_tport_tbl_reinit (void)
{
  const size_t sizes[] = { 4, 2, 2, 64, 3, 128, 1 };
  size_t old_n = 0, cap;
  mach_port_t *uports, *kports, *old_uports = NULL, *old_kports = NULL;

  for (int i = 0; i < countof (sizes); i++)
    {
      const size_t n = sizes[i];

      /* Capacity of a fresh table of this size, for comparison below */
      cap = tport_tbl_capacity (n);
      tport_tbl_free ();

      /* Re-create the previous iteration's table, then re-initialize
         it without tport_tbl_free */
      if (old_n != 0)
        {
          tport_tbl_init (old_n);
          for (size_t j = 0; j < old_n; j++)
            T_CHECK (tport_tbl_insert (old_uports[j], old_kports[j]) == 0);
        }
      tport_tbl_init (n);

      for (size_t j = 0; j < old_n; j++)
        {
          T_CHECK (tport_tbl_user_to_kern (old_uports[j]) == MACH_PORT_NULL);
          T_CHECK (tport_tbl_kern_to_user (old_kports[j]) == MACH_PORT_NULL);
        }

      uports = xmalloc (cap * sizeof (*uports));
      kports = xmalloc (cap * sizeof (*kports));
      T_CHECK (tport_tbl_fill (cap, uports, kports) == cap);

      for (size_t j = 0; j < cap; j++)
        {
          T_CHECK (tport_tbl_user_to_kern (uports[j]) == kports[j]);
          T_CHECK (tport_tbl_kern_to_user (kports[j]) == uports[j]);
        }

      free (old_uports);
      free (old_kports);
      old_uports = uports;
      old_kports = kports;
      /* The next iteration re-creates this table with only the first
         n entries, i.e. filled to its intended size */
      old_n = n;
    }

  free (old_uports);
  free (old_kports);
  tport_tbl_free ();
  return 0;
}

/* Deleted entries stay in their slot, so lookups must probe past them
   to reach entries further along the same chain, and must stop at the
   first empty slot when the key is absent. The chain starts two slots
   before the end of the table so it also wraps around to slot 0. */
static int
test_tport_tbl_probe_chain (void)
{
  enum { chain_len = 6 };
  const size_t n = 8;
  size_t nslots, home;
  mach_port_t expected, uports[chain_len], kports[chain_len], absent;

  nslots = tport_tbl_capacity (n);
  T_CHECK (nslots > (size_t) chain_len);
  home = nslots - 2;

  tport_tbl_init (n);
  for (int i = 0; i < chain_len; i++)
    {
      uports[i] = next_mach_port_at (home, nslots);
      kports[i] = next_mach_port ();
      T_CHECK (tport_tbl_insert (uports[i], kports[i]) == 0);
    }

  /* Delete every other entry along the chain, including its head */
  for (int i = 0; i < chain_len; i += 2)
    T_CHECK (tport_tbl_delete (uports[i]) == 0);

  for (int i = 0; i < chain_len; i++)
    {
      if (i % 2 == 0)
        T_CHECK (tport_tbl_user_to_kern (uports[i]) == MACH_PORT_DEAD);
      else
        {
          T_CHECK (tport_tbl_user_to_kern (uports[i]) == kports[i]);
          T_CHECK (tport_tbl_kern_to_user (kports[i]) == uports[i]);
        }
    }

  /* An absent key with the same home slot walks the whole chain,
     including deleted entries, and stops at the empty slot after it */
  absent = next_mach_port_at (home, nslots);
  T_CHECK (tport_tbl_user_to_kern (absent) == MACH_PORT_NULL);

  tport_tbl_free ();

  /* Same property without constructing collisions: delete every other
     entry from a table filled to its intended size, then check that the
     remaining entries still translate. */
  for (size_t size = 1; size <= 512; size = size * 2 + 1)
    {
      mach_port_t *u = xmalloc (size * sizeof (*u));
      mach_port_t *k = xmalloc (size * sizeof (*k));

      tport_tbl_init (size);
      for (size_t i = 0; i < size; i++)
        {
          u[i] = next_mach_port ();
          k[i] = next_mach_port ();
          T_CHECK (tport_tbl_insert (u[i], k[i]) == 0);
        }

      for (size_t i = 0; i < size; i += 2)
        T_CHECK (tport_tbl_delete (u[i]) == 0);

      for (size_t i = 0; i < size; i++)
        {
          expected = ((i % 2 == 0) ? MACH_PORT_DEAD : k[i]);
          T_CHECK (tport_tbl_user_to_kern (u[i]) == expected);
        }

      tport_tbl_free ();
      free (u);
      free (k);
    }

  return 0;
}

/* Fill every slot, not just the intended size. With no empty slot,
   lookups of absent keys must still terminate and return
   MACH_PORT_NULL, and further inserts must fail. */
static int
test_tport_tbl_full (void)
{
  mach_port_t uport, kport;
  const size_t sizes[] = { 1, 2, 3, 7, 16, 100, 257 };

  for (int s = 0; s < countof (sizes); s++)
    {
      const size_t n = sizes[s];
      const size_t max = n * TPORT_MAX_SLOTS_PER_ENTRY;
      size_t nslots;
      mach_port_t *uports, *kports;

      uports = xmalloc (max * sizeof (*uports));
      kports = xmalloc (max * sizeof (*kports));

      tport_tbl_init (n);
      nslots = tport_tbl_fill (max, uports, kports);
      T_CHECK (nslots >= n && nslots <= max);

      uport = next_mach_port ();
      kport = next_mach_port ();
      T_CHECK (tport_tbl_insert (uport, kport) == -1);

      for (size_t i = 0; i < nslots; i++)
        {
          T_CHECK (tport_tbl_user_to_kern (uports[i]) == kports[i]);
          T_CHECK (tport_tbl_kern_to_user (kports[i]) == uports[i]);
        }

      T_CHECK (tport_tbl_user_to_kern (next_mach_port ()) == MACH_PORT_NULL);
      T_CHECK (tport_tbl_kern_to_user (next_mach_port ()) == MACH_PORT_NULL);

      /* Deleting entries does not free slots */
      for (size_t i = 0; i < nslots; i++)
        T_CHECK (tport_tbl_delete (uports[i]) == 0);

      uport = next_mach_port ();
      kport = next_mach_port ();
      T_CHECK (tport_tbl_insert (uport, kport) == -1);

      uport = next_mach_port ();
      T_CHECK (tport_tbl_user_to_kern (uport) == MACH_PORT_NULL);

      for (size_t i = 0; i < nslots; i++)
        T_CHECK (tport_tbl_user_to_kern (uports[i]) == MACH_PORT_DEAD);

      tport_tbl_free ();
      free (uports);
      free (kports);
    }

  return 0;
}

/* kern_to_user scans the whole table rather than a probe chain, so it
   must not stop at an empty slot. After a deletion, the old kport no
   longer translates (the kernel may reuse that name for a new thread),
   and the MACH_PORT_DEAD stored in deleted entries is never matched. */
static int
test_tport_tbl_reverse (void)
{
  enum { N = 64 };
  mach_port_t expected, uports[N], kports[N];

  tport_tbl_init (N);
  for (size_t i = 0; i < N; i++)
    {
      uports[i] = next_mach_port ();
      kports[i] = next_mach_port ();
      T_CHECK (tport_tbl_insert (uports[i], kports[i]) == 0);
    }

  /* Check after all inserts, so entries sit behind empty slots */
  for (size_t i = 0; i < N; i++)
    T_CHECK (tport_tbl_kern_to_user (kports[i]) == uports[i]);

  for (size_t i = 0; i < N; i += 2)
    T_CHECK (tport_tbl_delete (uports[i]) == 0);

  for (size_t i = 0; i < N; i++)
    {
      expected = ((i % 2 == 0) ? MACH_PORT_NULL : uports[i]);
      T_CHECK (tport_tbl_kern_to_user (kports[i]) == expected);
    }

  T_CHECK (tport_tbl_kern_to_user (MACH_PORT_DEAD) == MACH_PORT_NULL);

  tport_tbl_free ();
  return 0;
}

/* MACH_PORT_NULL and MACH_PORT_DEAD are never valid as either half of
   an entry: inserts fail without occupying a slot, deletes fail, and
   lookups return MACH_PORT_NULL. Deleting a key that was never
   inserted also fails. */
static int
test_tport_tbl_invalid (void)
{
  const mach_port_t invalid[] = { MACH_PORT_NULL, MACH_PORT_DEAD };
  mach_port_t uport = next_mach_port (), kport = next_mach_port ();
  size_t cap;

  cap = tport_tbl_capacity (4);
  tport_tbl_init (4);

  for (int i = 0; i < countof (invalid); i++)
    {
      T_CHECK (tport_tbl_insert (invalid[i], kport) == -1);
      T_CHECK (tport_tbl_insert (uport, invalid[i]) == -1);
      T_CHECK (tport_tbl_delete (invalid[i]) == -1);
      T_CHECK (tport_tbl_user_to_kern (invalid[i]) == MACH_PORT_NULL);
      T_CHECK (tport_tbl_kern_to_user (invalid[i]) == MACH_PORT_NULL);
    }
  T_CHECK (tport_tbl_insert (MACH_PORT_NULL, MACH_PORT_DEAD) == -1);
  T_CHECK (tport_tbl_insert (MACH_PORT_DEAD, MACH_PORT_NULL) == -1);

  T_CHECK (tport_tbl_user_to_kern (uport) == MACH_PORT_NULL);
  T_CHECK (tport_tbl_kern_to_user (kport) == MACH_PORT_NULL);
  T_CHECK (tport_tbl_delete (uport) == -1);

  /* Invalid lookups still return MACH_PORT_NULL with live entries */
  T_CHECK (tport_tbl_insert (uport, kport) == 0);
  for (int i = 0; i < countof (invalid); i++)
    {
      T_CHECK (tport_tbl_user_to_kern (invalid[i]) == MACH_PORT_NULL);
      T_CHECK (tport_tbl_kern_to_user (invalid[i]) == MACH_PORT_NULL);
    }

  /* None of the failed inserts occupied a slot */
  T_CHECK (tport_tbl_fill (cap, NULL, NULL) == cap - 1);

  tport_tbl_free ();
  return 0;
}

static int
do_test (void)
{
  struct subtest tests[] =
    {
      SUBTEST_DECL (test_tport_tbl_uninit),
      SUBTEST_DECL (test_tport_tbl_basic),
      SUBTEST_DECL (test_tport_tbl_reinit),
      SUBTEST_DECL (test_tport_tbl_probe_chain),
      SUBTEST_DECL (test_tport_tbl_full),
      SUBTEST_DECL (test_tport_tbl_reverse),
      SUBTEST_DECL (test_tport_tbl_invalid),
      SUBTEST_DECL (test_tport_tbl_threaded),
    };

  for (int i = 0; i < countof (tests); i++)
    {
      if (SUBTEST_CHECK (tests[i]) != 0)
        return 1;
    }

  return 0;
}

#define TEST_NAME "tst-thread-port"
#define TEST_FUNCTION do_test
#include "tst-support.c"
