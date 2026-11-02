/* thread-priority-defs.h */
#ifndef XND_THREAD_PRIORITY_DEFS_H
#define XND_THREAD_PRIORITY_DEFS_H

#include "common/compiler.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/qos.h>

/* Definitions of mach thread priority, pthread priority, and qos class
   related constants not available in public headers, and utility
   functions for working with these priority values.

   Sources:
    xnu/osfmk/mach/thread_policy_private.h
    xnu/bsd/pthread/priority_private.h
    libpthread/private/sys/qos_private.h */

/* qos class not exposed in public headers */
#ifndef QOS_CLASS_MAINTENANCE
# define QOS_CLASS_MAINTENANCE ((qos_class_t) 0x5)
#endif

/* mach thread priority */
typedef uint8_t thread_qos_t;

#define THREAD_QOS_UNSPECIFIED     	0
#define THREAD_QOS_DEFAULT         	THREAD_QOS_UNSPECIFIED
#define THREAD_QOS_MAINTENANCE     	1
#define THREAD_QOS_BACKGROUND      	2
#define THREAD_QOS_UTILITY         	3
#define THREAD_QOS_LEGACY          	4
#define THREAD_QOS_USER_INITIATED  	5
#define THREAD_QOS_USER_INTERACTIVE	6
#define THREAD_QOS_LAST            	7

/* pthread priority */
typedef unsigned long pthread_priority_t;

#define _PTHREAD_PRIORITY_OVERCOMMIT_FLAG   	0x80000000u
#define _PTHREAD_PRIORITY_INHERIT_FLAG      	0x40000000u
#define _PTHREAD_PRIORITY_ROOTQUEUE_FLAG    	0x20000000u
#define _PTHREAD_PRIORITY_SCHED_PRI_FLAG    	0x20000000u
#define _PTHREAD_PRIORITY_ENFORCE_FLAG      	0x10000000u
#define _PTHREAD_PRIORITY_FALLBACK_FLAG     	0x04000000u
#define _PTHREAD_PRIORITY_COOPERATIVE_FLAG  	0x08000000u
#define _PTHREAD_PRIORITY_EVENT_MANAGER_FLAG	0x02000000u
#define _PTHREAD_PRIORITY_NEEDS_UNBIND_FLAG 	0x01000000u
#define _PTHREAD_PRIORITY_OVERRIDE_QOS_FLAG 	0x00800000u

#define _PTHREAD_PRIORITY_THREAD_TYPE_MASK \
  (_PTHREAD_PRIORITY_COOPERATIVE_FLAG | _PTHREAD_PRIORITY_OVERCOMMIT_FLAG)

#define _PTHREAD_PRIORITY_FLAGS_MASK                    0xff000000u
#define _PTHREAD_PRIORITY_SCHED_PRI_MASK                0x0000ffffu

#define _PTHREAD_PRIORITY_QOS_CLASS_MASK                0x003fff00u
#define _PTHREAD_PRIORITY_QOS_CLASS_SHIFT               (8ull)
#define _PTHREAD_PRIORITY_VALID_QOS_CLASS_MASK          0x00003f00u
#define _PTHREAD_PRIORITY_VALID_OVERRIDE_QOS_MASK       0x003fc000u
#define _PTHREAD_PRIORITY_QOS_OVERRIDE_SHIFT            (14ull)

#define _PTHREAD_PRIORITY_PRIORITY_MASK                 0x000000ffu
#define _PTHREAD_PRIORITY_PRIORITY_SHIFT                (0)

/* translate qos class to thread qos */
__attribute_const__
static inline thread_qos_t
_pthread_qos_class_to_thread_qos (qos_class_t qos)
{
  switch (qos)
    {
    case QOS_CLASS_USER_INTERACTIVE:
      return THREAD_QOS_USER_INTERACTIVE;
    case QOS_CLASS_USER_INITIATED:
      return THREAD_QOS_USER_INITIATED;
    case QOS_CLASS_DEFAULT:
      return THREAD_QOS_LEGACY;
    case QOS_CLASS_UTILITY:
      return THREAD_QOS_UTILITY;
    case QOS_CLASS_BACKGROUND:
      return THREAD_QOS_BACKGROUND;
    case QOS_CLASS_MAINTENANCE:
      return THREAD_QOS_MAINTENANCE;
    default:
      return THREAD_QOS_UNSPECIFIED;
    }
}

/* translate thread qos to qos class */
__attribute_const__
static inline qos_class_t
_pthread_qos_class_from_thread_qos (thread_qos_t qos)
{
  switch (qos)
    {
    case THREAD_QOS_MAINTENANCE:
      return QOS_CLASS_MAINTENANCE;
    case THREAD_QOS_BACKGROUND:
      return QOS_CLASS_BACKGROUND;
    case THREAD_QOS_UTILITY:
      return QOS_CLASS_UTILITY;
    case THREAD_QOS_LEGACY:
      return QOS_CLASS_DEFAULT;
    case THREAD_QOS_USER_INITIATED:
      return QOS_CLASS_USER_INITIATED;
    case THREAD_QOS_USER_INTERACTIVE:
      return QOS_CLASS_USER_INTERACTIVE;
    default:
      return QOS_CLASS_UNSPECIFIED;
    }
}

__attribute_const__
static inline bool
_pthread_priority_has_qos (pthread_priority_t pp)
{
  return (((pp & (_PTHREAD_PRIORITY_SCHED_PRI_FLAG
                  | _PTHREAD_PRIORITY_EVENT_MANAGER_FLAG)) == 0)
          && ((pp & _PTHREAD_PRIORITY_VALID_QOS_CLASS_MASK) != 0));
}

__attribute_const__
static inline thread_qos_t
_pthread_priority_thread_qos (pthread_priority_t pp)
{
  thread_qos_t qos = THREAD_QOS_UNSPECIFIED;
  
  if (_pthread_priority_has_qos (pp))
    {
      pp &= _PTHREAD_PRIORITY_VALID_QOS_CLASS_MASK;
      pp >>= _PTHREAD_PRIORITY_QOS_CLASS_SHIFT;
      qos = (thread_qos_t) __builtin_ffs ((int) pp);
    }

  return qos;
}

/* relative priority in [QOS_MIN_RELATIVE_PRIORITY, 0], stored as
   (relpri - 1) in the low byte */
__attribute_const__
static inline int
_pthread_priority_relpri (pthread_priority_t pp)
{
  int relpri = 0;

  if (_pthread_priority_has_qos (pp))
    {
      pp &= _PTHREAD_PRIORITY_PRIORITY_MASK;
      pp >>= _PTHREAD_PRIORITY_PRIORITY_SHIFT;
      relpri = (int) ((int8_t) pp + 1);
    }

  return relpri;
}

__attribute_const__
static inline bool
_pthread_priority_has_override_qos (pthread_priority_t pp)
{
  return (((pp & (_PTHREAD_PRIORITY_SCHED_PRI_FLAG
                  | _PTHREAD_PRIORITY_EVENT_MANAGER_FLAG)) == 0)
          && ((pp & _PTHREAD_PRIORITY_OVERRIDE_QOS_FLAG) != 0)
          && ((pp & _PTHREAD_PRIORITY_VALID_OVERRIDE_QOS_MASK) != 0));
}

__attribute_const__
static inline thread_qos_t
_pthread_priority_thread_override_qos (pthread_priority_t pp)
{
  thread_qos_t qos = THREAD_QOS_UNSPECIFIED;

  if (_pthread_priority_has_override_qos (pp))
    {
      pp &= _PTHREAD_PRIORITY_VALID_OVERRIDE_QOS_MASK;
      pp >>= _PTHREAD_PRIORITY_QOS_OVERRIDE_SHIFT;
      qos = (thread_qos_t) __builtin_ffs ((int) pp);
    }

  return qos;
}

__attribute_const__
static inline pthread_priority_t
_pthread_priority_make_from_thread_qos (thread_qos_t qos, int pri,
                                        unsigned long flags)
{
  pthread_priority_t pp;

  pp = (flags & _PTHREAD_PRIORITY_FLAGS_MASK);
  if ((qos != THREAD_QOS_UNSPECIFIED) && (qos < THREAD_QOS_LAST))
    {
      pp |= (1ul << (_PTHREAD_PRIORITY_QOS_CLASS_SHIFT + qos - 1));
      pp |= (((uint8_t) pri - 1) & _PTHREAD_PRIORITY_PRIORITY_MASK);
    }

  return pp;
}

#define _pthread_priority_is_cooperative(pp) \
  (((pp) & _PTHREAD_PRIORITY_COOPERATIVE_FLAG) != 0)
#define _pthread_priority_is_overcommit(pp) \
  (((pp) & _PTHREAD_PRIORITY_OVERCOMMIT_FLAG) != 0)
#define _pthread_priority_is_nonovercommit(pp) \
  (!_pthread_priority_is_cooperative (pp) \
   && !_pthread_priority_is_overcommit (pp))

#endif /* XND_THREAD_PRIORITY_DEFS_H */
