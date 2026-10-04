/* wq_wrappers.c */
#include <errno.h>
#include <pthread/pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#include "xnd/xnd.h"
#include "xnd/xnd_lib.h"
#include "xnd/interpose.h"
#include "xnd/thread_info.h"
#include "xnd/workq.h"
#include "xnd/util/env.h"
#include "pthread_wrappers.h"
#include "wq_wrappers.h"

#if DEBUG || DEVELOPMENT
static inline const char *
wqops_string (int op)
{
  switch (op)
    {
    case WQOPS_THREAD_RETURN:
      return "WQOPS_THREAD_RETURN";
    case WQOPS_QUEUE_NEWSPISUPP:
      return "WQOPS_QUEUE_NEWSPISUPP";
    case WQOPS_QUEUE_REQTHREADS:
      return "WQOPS_QUEUE_REQTHREADS";
    case WQOPS_QUEUE_REQTHREADS2:
      return "WQOPS_QUEUE_REQTHREADS2";
    case WQOPS_THREAD_KEVENT_RETURN:
      return "WQOPS_THREAD_KEVENT_RETURN";
    case WQOPS_SET_EVENT_MANAGER_PRIORITY:
      return "WQOPS_SET_EVENT_MANAGER_PRIORITY";
    case WQOPS_THREAD_WORKLOOP_RETURN:
      return "WQOPS_THREAD_WORKLOOP_RETURN";
    case WQOPS_SHOULD_NARROW:
      return "WQOPS_SHOULD_NARROW";
    case WQOPS_SETUP_DISPATCH:
      return "WQOPS_SETUP_DISPATCH";
    default:
      return NULL;
    }
}
#endif

/*
 * Internal trampoline functions, allowing libxnd to get control of
 * a workqueue thread before (and after) passing control to the real
 * workqueue thread callback function.
 */
static void workq_tramp WORKQ_CB_ARGS;
static void kevent_tramp KEVENT_CB_ARGS;
static void workloop_tramp WORKLOOP_CB_ARGS;

/*
 * Function pointers to real workqueue callback functions,
 * initialized by pthread_workqueue_setup_hook ().
 */
static WORKQ_CB (real_workq_cb);
static KEVENT_CB (real_kevent_cb);
static WORKLOOP_CB (real_workloop_cb);

static void
workq_tramp WORKQ_CB_ARGS
{
  workq_thread_prepare ();
  (*real_workq_cb) (arg1);
}

static void
kevent_tramp KEVENT_CB_ARGS
{
  workq_thread_prepare ();
  (*real_kevent_cb) (arg1, arg2);
}

static void
workloop_tramp WORKLOOP_CB_ARGS
{
  workq_thread_prepare ();
  (*real_workloop_cb) (arg1, arg2, arg3);
}

static int
pthread_workqueue_setup_hook (struct pthread_workqueue_config *cfg,
			      size_t cfg_size)
{
  struct workq_dispatch_config wdc_cfg;

  if (cfg == NULL || cfg_size < offsetof (typeof (*cfg), kevent_cb)
      || cfg->version > PTHREAD_WORKQUEUE_CONFIG_VERSION)
    return EINVAL;

  if ((cfg->version < PTHREAD_WORKQUEUE_CONFIG_MIN_SUPPORTED_VERSION)
      || (cfg->flags & ~PTHREAD_WORKQUEUE_CONFIG_SUPPORTED_FLAGS))
    return ENOTSUP;

  if (cfg->version == 1)
    {
      if (cfg_size < offsetof (typeof (*cfg), queue_label_offs))
	return EINVAL;
    }
  else if (cfg_size < sizeof (*cfg)) /* version == 2 */
    return EINVAL;

  wdc_cfg.wdc_version = WORKQ_DISPATCH_CONFIG_VERSION;
  wdc_cfg.wdc_flags = 0;
  wdc_cfg.wdc_queue_serialno_offs = cfg->queue_serialno_offs;
  wdc_cfg.wdc_queue_label_offs = cfg->queue_label_offs;

  struct workq_kernreturn_args args = {
    WQOPS_SETUP_DISPATCH, &wdc_cfg, sizeof (wdc_cfg), 0
  };

  return workq_setup_dispatch_hook (&args);
}
INTERPOSE (pthread_workqueue_setup_hook, pthread_workqueue_setup);

static int
_pthread_workqueue_init_hook (WORKQ_CB (workq_cb), int offset,
			      __unused int flags)
{
  struct pthread_workqueue_config cfg =
    {
      .flags = 0,
      .version = PTHREAD_WORKQUEUE_CONFIG_VERSION,
      .kevent_cb = NULL,
      .workloop_cb = NULL,
      .workq_cb = workq_cb,
      .queue_serialno_offs = offset,
      .queue_label_offs = 0,
    };

  return pthread_workqueue_setup_hook (&cfg, sizeof (cfg));
}
INTERPOSE (_pthread_workqueue_init_hook, _pthread_workqueue_init);

static int
_pthread_workqueue_init_with_kevent_hook (WORKQ_CB (workq_cb),
					  KEVENT_CB (kevent_cb),
					  int offset, __unused int flags)
{
   struct pthread_workqueue_config cfg =
    {
      .flags = 0,
      .version = PTHREAD_WORKQUEUE_CONFIG_VERSION,
      .kevent_cb = kevent_cb,
      .workloop_cb = NULL,
      .workq_cb = workq_cb,
      .queue_serialno_offs = offset,
      .queue_label_offs = 0,
    };

   return pthread_workqueue_setup_hook (&cfg, sizeof (cfg));
}
INTERPOSE (_pthread_workqueue_init_with_kevent_hook,
	   _pthread_workqueue_init_with_kevent);

static int
_pthread_workqueue_init_with_workloop_hook (WORKQ_CB (workq_cb),
					    KEVENT_CB (kevent_cb),
					    WORKLOOP_CB (workloop_cb),
					    int offset, __unused int flags)
{
   struct pthread_workqueue_config cfg =
    {
      .flags = 0,
      .version = PTHREAD_WORKQUEUE_CONFIG_VERSION,
      .kevent_cb = kevent_cb,
      .workloop_cb = workloop_cb,
      .workq_cb = workq_cb,
      .queue_serialno_offs = offset,
      .queue_label_offs = 0,
    };

   return pthread_workqueue_setup_hook (&cfg, sizeof (cfg));
}
INTERPOSE (_pthread_workqueue_init_with_workloop_hook,
	   _pthread_workqueue_init_with_workloop);

static int
__workq_kernreturn_hook (int op, void *arg2, int arg3, int arg4)
{
  int ret;
  struct workq_kernreturn_args args = { op, arg2, arg3, arg4 };

  trace_workq_kernreturn (op, arg2, arg3, arg4);

  switch (op)
    {
    case WQOPS_THREAD_RETURN:
    case WQOPS_THREAD_KEVENT_RETURN:
    case WQOPS_THREAD_WORKLOOP_RETURN:
      workq_thread_return_hook (&args);
      unreachable ();
    case WQOPS_QUEUE_REQTHREADS:
    case WQOPS_QUEUE_REQTHREADS2:
      ret = workq_reqthreads_hook (&args);
      break;
    case WQOPS_SETUP_DISPATCH:
      ret = workq_setup_dispatch_hook (&args);
      break;
    default:
      ret = __workq_kernreturn (op, arg2, arg3, arg4);
      break;
    }

  return ret;
}
INTERPOSE (__workq_kernreturn_hook, __workq_kernreturn);
