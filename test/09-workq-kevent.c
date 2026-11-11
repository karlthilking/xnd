/* 09-workq-kevent.c */
#include <assert.h>
#include <Block.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/queue.h>

#include "common/atomic.h"
#include "common/compiler.h"
#include "common/time.h"
#include "common/xalloc.h"
#include "common/xthread.h"
#include "common/xunistd.h"
#include "support/xworkqueue.h"
#include "thread-priority-defs.h"

extern pthread_t pthread_main_thread_np (void);

struct udata_args
{
  uint16_t count;
  void (^cb) (void);
};

struct block 
{
  void (^cb) (void);
  TAILQ_ENTRY (block) link;
};

struct block_queue
{
  pthread_mutex_t lock;
  dispatch_semaphore_t sem_prod;
  dispatch_semaphore_t sem_cons;
  TAILQ_HEAD (block_head, block) head;
};

static float rand_float (float, float);

static struct block *block_alloc (void (^) (void));
static void block_free (struct block *);

static void queue_init (void);
static void queue_fini (void);
static void queue_cons_post (void);
static void queue_cons_wait (void);
static void queue_prod_post (uint16_t);
static void queue_prod_wait (uint16_t);
static struct block *queue_pop (void);
static void queue_push (struct block *);

static void worker_thread (pthread_priority_t);
static void request_worker (thread_qos_t);
static void handle_timer_kevent (struct kevent_qos_s *);
static uint16_t handle_user_kevent (const struct kevent_qos_s *);
static void kevent_thread (void **, int *);

static void register_timer_kevent (uint64_t);
static void register_user_kevent (void);
static void trigger_user_kevent (uint64_t);

static void main_thread_signal (void);
static void main_thread_wait (void);
static void main_thread_siginit (void);
static void progress (int, uint64_t);

static struct block_queue *queue = NULL;
static uint64_t kevent_timer_cnt = 0;
static uint64_t kevent_user_cnt = 0;

static float
rand_float(float lo, float hi)
{
  return ((float) drand48 () * (hi - lo)) + lo;
}

static struct block *
block_alloc (void (^cb) (void))
{
  struct block *block;

  block = xmalloc (sizeof (*block));
  block->cb = Block_copy (cb);

  return block;
}

static void
block_free (struct block *block)
{
  Block_release (block->cb);
  free (block);
}

static void
queue_init (void)
{
  queue = xmalloc (sizeof (*queue));
  TAILQ_INIT (&queue->head);

  queue->sem_prod = dispatch_semaphore_create (0);
  queue->sem_cons = dispatch_semaphore_create (0);

  xpthread_mutex_init (&queue->lock, NULL);
}

static void
queue_fini (void)
{
  struct block *block, *next;
  
  TAILQ_FOREACH_SAFE (block, &queue->head, link, next)
    block_free (block);

  xpthread_mutex_destroy (&queue->lock);
  free (queue);
}

static void
queue_cons_post (void)
{
  dispatch_semaphore_signal (queue->sem_prod);
}

static void
queue_cons_wait (void)
{
  dispatch_semaphore_wait (queue->sem_cons, DISPATCH_TIME_FOREVER);
}

static void
queue_prod_post (uint16_t count)
{
  for (uint16_t i = 0; i < count; i++)
    dispatch_semaphore_signal (queue->sem_cons);
}

static void
queue_prod_wait (uint16_t count)
{
  for (uint16_t i = 0; i < count; i++)
    dispatch_semaphore_wait (queue->sem_prod, DISPATCH_TIME_FOREVER);
}

static struct block *
queue_pop (void)
{
  struct block *block;

  xpthread_mutex_lock (&queue->lock);
  block = TAILQ_FIRST (&queue->head);
  TAILQ_REMOVE (&queue->head, block, link);
  xpthread_mutex_unlock (&queue->lock);

  return block;
}

static void
queue_push (struct block *block)
{
  xpthread_mutex_lock (&queue->lock);
  TAILQ_INSERT_TAIL (&queue->head, block, link);
  xpthread_mutex_unlock (&queue->lock);
}

static void
worker_thread (pthread_priority_t pp)
{
  struct block *block;

  queue_cons_wait ();

  /* Execute exactly one block per schedule */
  block = queue_pop ();
  block->cb ();
  block_free (block);

  /* Signal producer */
  queue_cons_post ();
}

static void
request_worker (thread_qos_t qos)
{
  pthread_priority_t pp;

  pp = _pthread_priority_make_from_thread_qos (qos, 0, 0);
  xworkq_reqthreads (1, pp, false);
}

static void
handle_timer_kevent (struct kevent_qos_s *kev)
{
  uint64_t timeout;

  assert ((kev->flags & EV_ADD) != 0);
  assert ((kev->flags & EV_ONESHOT) != 0);
  
  /* Timer delay chosen by the main thread is stored in the
     kevent's udata field */
  timeout = mach_absolute_time () + kev->udata;
  kev->data = (int64_t) timeout;
  kev->fflags = NOTE_ABSOLUTE | NOTE_MACHTIME;
}

static uint16_t
handle_user_kevent (const struct kevent_qos_s *kev)
{
  struct block *block;
  struct udata_args *args = (struct udata_args *) kev->udata;

  for (uint16_t i = 0; i < args->count; i++)
    {
      block = block_alloc (args->cb);
      queue_push (block);
      request_worker (THREAD_QOS_UTILITY);
    }

  queue_prod_post (args->count);
  return args->count;
}

static void
kevent_thread (void **events, int *nevents)
{
  uint16_t count = 0;
  struct kevent_qos_s *in, *out, *kevents;

  kevents = *(struct kevent_qos_s **) events;
  out = kevents;

  for (in = kevents; in != kevents + *nevents; in++)
    {
      if ((in->flags & EV_ERROR) != 0)
        {
          fprintf (stderr, "kevent error: %s\n", strerror (in->data));
          continue;
        }
      switch (in->filter)
        {
        case EVFILT_USER:
          count = handle_user_kevent (in);
          xnd_atomic_inc_relaxed (&kevent_user_cnt);
          *out++ = *in;
          break;
        case EVFILT_TIMER:
          handle_timer_kevent (in);
          xnd_atomic_inc_relaxed (&kevent_timer_cnt);
          *out++ = *in;
          break;
        default:
          break;
        }
    }

  /* Wait for consumers to execute count blocks, then signal for the
     main thread to continue. */
  if (count != 0)
    {
      queue_prod_wait (count);
      main_thread_signal ();
    }

  *nevents = (int) (out - kevents);
}

static void
register_timer_kevent (uint64_t delay)
{
  const struct kevent_qos_s kev =
    {
      .ident = 1,
      .filter = EVFILT_TIMER,
      .flags = EV_ADD | EV_ONESHOT,
      .qos = _PTHREAD_PRIORITY_EVENT_MANAGER_FLAG,
      .udata = delay,
      .fflags = NOTE_ABSOLUTE | NOTE_MACHTIME,
      .data = (int64_t) (mach_absolute_time () + delay),
    };
  xworkq_kevent_submit (&kev, 1);
}

static void
register_user_kevent (void)
{
  const struct kevent_qos_s kev =
    {
      .ident = 1,
      .filter = EVFILT_USER,
      .flags = EV_ADD | EV_CLEAR,
      .qos = _PTHREAD_PRIORITY_EVENT_MANAGER_FLAG,
    };
  xworkq_kevent_submit (&kev, 1);
}

static void
trigger_user_kevent (uint64_t udata)
{
  const struct kevent_qos_s kev =
    {
      .ident = 1,
      .filter = EVFILT_USER,
      .flags = 0,
      .fflags = NOTE_TRIGGER,
      .data = 0,
      .udata = udata,
    };
  xworkq_kevent_submit (&kev, 1);
}

static void
main_thread_signal (void)
{
  int err;
  static pthread_t main_thread = NULL;

  if (main_thread == NULL)
    main_thread = pthread_main_thread_np ();

  err = pthread_kill (main_thread, SIGUSR1);
  if (err != 0)
    {
      fprintf (stderr, "pthread_kill: %s\n", strerror (err));
      abort ();
    }
}

static void
main_thread_wait (void)
{
  int ret, sig;
  const sigset_t mask = sigmask (SIGUSR1);

  ret = sigwait (&mask, &sig);
  if (ret != 0)
    {
      perror ("sigwait");
      abort ();
    }
  assert (sig == SIGUSR1);
}

static void
main_thread_siginit (void)
{
  const sigset_t mask = sigmask (SIGUSR1);

  xpthread_sigmask (SIG_BLOCK, &mask, NULL);
  if (signal (SIGUSR1, SIG_DFL) == SIG_ERR)
    {
      perror ("signal");
      abort ();
    }
}

static void
progress (int iter, uint64_t block_cnt)
{
  printf ("(%d)\tblocks: %llu\ttimer events: %llu\tuser events: %llu\n",
          iter, block_cnt, xnd_atomic_load_relaxed (&kevent_timer_cnt),
          xnd_atomic_load_relaxed (&kevent_user_cnt));
}

int
main (int argc, char *argv[])
{
  uint16_t cnt;
  int iter, niters = 2500;
  uint64_t block_cnt = 0;
  struct udata_args args = {0};
  struct pthread_workqueue_config cfg =
    {
      .flags = 0,
      .version = PTHREAD_WORKQUEUE_CONFIG_VERSION,
      .workq_cb = worker_thread,
      .kevent_cb = kevent_thread,
      .workloop_cb = NULL,
      .queue_serialno_offs = 0,
      .queue_label_offs = 0,
    };

#define shift() *(argc--, argv++)
  const char *opt = NULL, *val = NULL;
  shift ();
  while (argc > 0)
    {
      opt = shift ();
      if (strcmp (opt, "--iterations") == 0)
        {
          if ((val = shift ()) != NULL)
            niters = atoi (val);
        }
    }
#undef shift

  main_thread_siginit ();
  xworkq_setup (&cfg);
  queue_init ();

  /* Set event manager priority */
  {
    thread_qos_t qos;
    pthread_priority_t pp;

    qos = THREAD_QOS_USER_INITIATED;
    pp = _pthread_priority_make_from_thread_qos (qos, 0, 0);
    xworkq_set_event_mgr_pri (pp);
  }

  args.cb = ^(void)
    {
      float *A, *b, *x;
      size_t m, n;
      const float lo = -25.0f, hi = 25.0f;
      float (^randf) (void) = ^(void) { return rand_float (lo, hi); };

      m = (size_t) (arc4random () % 64);
      n = (size_t) (arc4random () % 64);

      A = xmalloc (m * n * sizeof (float));
      b = xmalloc (n * sizeof (float));
      x = xmalloc (m * sizeof (float));

      for (int c = 50; c > 0; c--)
        {
          memset (x, 0x00, m * sizeof (float));
          for (int j = 0; j < n; j++)
            *(b + j) = randf ();
          for (int i = 0; i < m; i++)
            for (int j = 0; j < n; j++)
              *(A + i * n + j) = randf ();
          for (size_t i = 0; i < m; i++)
            for (size_t j = 0; j < n; j++)
              *(x + i) += *(A + i * n + j) * *(b + j);
        }

      free (A);
      free (b);
      free (x);
    };

  register_timer_kevent (MSEC_TO_MACH_TIME (50));
  register_user_kevent ();

  for (iter = 1; iter <= niters; iter++)
    {
      cnt = (uint16_t) (arc4random () % 500) + 12;
      args.count = cnt;

      trigger_user_kevent ((uint64_t) &args);
      main_thread_wait ();
      block_cnt += cnt;

      if ((iter % 100) == 0)
        progress (iter, block_cnt);
    }

  queue_fini ();
  exit (0);
}
