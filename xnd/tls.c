/* tls.c */
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "xnd/xnd.h"
#include "xnd/tls.h"
#include "xnd/thread_info.h"
#include "wrappers/pthread_wrappers.h"

extern uintptr_t xnd_munge_token;
extern pthread_key_t tlv_flag_key;

__private_extern uintptr_t _pthread_ptr_munge_token = 0;

void
pthread_ptr_munge_save (void)
{
  pthread_t self = pthread_self ();
  _pthread_ptr_munge_token = ((uintptr_t) self ^ self->__sig);
}

void
pthread_ptr_munge_restore (void)
{
  pthread_t self = tsd_getspecific (__TSD_THREAD_SELF);
  self->__sig = ((uintptr_t) self ^ _pthread_ptr_munge_token);
  tsd_setspecific (__TSD_PTR_MUNGE, _pthread_ptr_munge_token);
}

/*
 * xnd_tlv_init:
 *  Use the xnd_key structure, tlv_flag_key, initialized in
 *  thread_list_init as storage for thread-local initialization flag.
 */
void
xnd_tlv_init (void)
{
  xpthread_setspecific (tlv_flag_key, NULL);
  (void) thread_self ();
  xpthread_setspecific (tlv_flag_key, XND_TLV_INIT);
}

/*
 * xnd_tlv_fini:
 *  Set tlv_flag_key's tsd value to XND_TLV_NULL, to mark that
 *  thread-local variables should not be accessed.
 */
void
xnd_tlv_fini (void)
{
  xpthread_setspecific (tlv_flag_key, NULL);
}

bool
xnd_tlv_ok (void)
{
  return (pthread_getspecific (tlv_flag_key) == XND_TLV_INIT);
}

static inline bool
check_tsd_pthread_struct_offset (void)
{
  uintptr_t tsd, self;
  bool ok;

  tsd = self_tsd_base ();
  self = (uintptr_t) pthread_self ();

  ok = ((self + PTHREAD_TSD_OFFSET == tsd)
        && (tsd + TSD_PTHREAD_OFFSET == self));

#if DEBUG || DEVELOPMENT
  if (!ok)
    xnd_error ("%s failed\n", __func__);
#endif

  return ok;
}

static inline bool
check_tsd_threadid_offset (void)
{
  u64 tsd_tid, pthread_tid, tid;
  uintptr_t tsd, self;
  bool ok;

  tsd = self_tsd_base ();
  self = (uintptr_t) pthread_self ();

  tid = __thread_selfid ();
  tsd_tid = *(u64 *) (tsd + TSD_THREADID_OFFSET);
  pthread_tid = *(u64 *) (self + PTHREAD_THREADID_OFFSET);

  ok = ((tsd_tid == tid) && (pthread_tid == tid));
#if DEBUG || DEVELOPMENT
  if (!ok)
    xnd_error ("%s failed\n", __func__);
#endif

  return ok;
}

static inline bool
check_pthread_mutex_tid_offset (void)
{
  bool ok;
  u64 tid, *tidaddr;
  pthread_mutex_t mutex;

  tid = __thread_selfid ();
  pthread_mutex_init (&mutex, NULL);
  pthread_mutex_lock (&mutex);

  tidaddr = (u64 *) ((uintptr_t) &mutex + PTHREAD_MUTEX_TID_OFFSET);
  ok = (*tidaddr == tid);

  pthread_mutex_unlock (&mutex);
  pthread_mutex_destroy (&mutex);

  return ok;
}

bool
do_tsd_runtime_checks (void)
{
  return (check_tsd_pthread_struct_offset () && check_tsd_threadid_offset ()
          && check_pthread_mutex_tid_offset ());
}
