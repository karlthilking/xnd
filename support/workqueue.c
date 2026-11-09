/* workqueue.c */
#include <errno.h>
#include <pthread.h>
#include "workqueue.h"

int
workq_setup (struct pthread_workqueue_config *cfg)
{
  return pthread_workqueue_setup (cfg, sizeof (*cfg));
}

int
workq_setup_workloop (void (*workq_cb) (pthread_priority_t),
                      void (*kevent_cb) (void **, int *),
                      void (*workloop_cb) (uint64_t *, void **, int *))
{
  struct pthread_workqueue_config cfg =
    {
      .flags = 0,
      .version = PTHREAD_WORKQUEUE_CONFIG_VERSION,
      .kevent_cb = kevent_cb,
      .workloop_cb = workloop_cb,
      .workq_cb = workq_cb,
      .queue_serialno_offs = 0,
      .queue_label_offs = 0,
    };

  return workq_setup (&cfg);
}

int
workq_setup_kevent (void (*workq_cb) (pthread_priority_t),
                    void (*kevent_cb) (void **, int *))
{
  return workq_setup_workloop (workq_cb, kevent_cb, NULL);
}

int
workq_setup_worker (void (*workq_cb) (pthread_priority_t))
{
  return workq_setup_kevent (workq_cb, NULL);
}

int
workq_reqthreads (int nthreads, pthread_priority_t pp, bool cooperative)
{
  int ret, op;

  op = (cooperative ? WQOPS_QUEUE_REQTHREADS2 : WQOPS_QUEUE_REQTHREADS);
  ret = __workq_kernreturn (op, NULL, nthreads, (int) pp);

  return (ret < 0 ? errno : 0);
}

int
workq_set_event_mgr_pri (pthread_priority_t pp)
{
  int ret;
  const int op = WQOPS_SET_EVENT_MANAGER_PRIORITY;

  ret = __workq_kernreturn (op, NULL, (int) pp, 0);

  return (ret < 0 ? errno : 0);
}

int
workq_kevent_submit (const struct kevent_qos_s *events, int nevents)
{
  int ret;
  const unsigned int flags = KEVENT_FLAG_WORKQ;

  ret = kevent_qos (-1, events, nevents, NULL, 0, NULL, NULL, flags);

  return (ret < 0 ? errno : 0);
}
