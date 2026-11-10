/* 09-workq-kevent.c */
#include <assert.h>
#include <Block.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
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
static void kevent_thread (void **, int *);

static void register_user_kevent (void);
static void trigger_user_kevent (uint64_t);

static struct block_queue *queue = NULL;

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
}

static void
kevent_thread (void **events, int *nevents)
{
  struct kevent_qos_s *kevents = *(struct kevent_qos_s **) events;
  struct kevent_qos_s *in, *out = kevents;

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
          handle_user_kevent (in);
          break;
        case EVFILT_TIMER:
          fprintf (stderr, "Unexpected EVFILT_TIMER kevent\n");
          break;
        default:
          break;
        }
    }

  *nevents = (int) (out - kevents);
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

int
main (int argc, char *argv[])
{
  void (^cb) (void);
  uint64_t total = 0;
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

  cb = ^(void)
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

  register_user_kevent ();
  for (int i = 0; i < 5000; i++)
    {
      uint16_t count = (uint16_t) (arc4random () % 500) + 12;
      struct udata_args args =
        {
          .count = count,
          .cb = cb,
        };

      trigger_user_kevent ((uint64_t) &args);
      queue_prod_wait (count);
      total += count;

      if ((i % 100) == 0)
        printf ("(iteration %d) blocks executed = %llu\n", i, total);
    }

  queue_fini ();
  exit (0);
}
