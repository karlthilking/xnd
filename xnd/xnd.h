/* xnd.h */
#ifndef XND_H
#define XND_H

#define XND_EXIT_SUCCESS 0
#define XND_EXIT_FAILURE 222

#define XND_DEFAULT_CKPT_SIGNAL SIGUSR2

#include "common/types.h"
#include "common/compiler.h"
#include "common/bits.h"
#include "common/xalloc.h"
#include "common/xthread.h"
#include "common/atomic.h"

/* brings in xnd_error, xnd_warn, xnd_panic, etc ... */
#include "util/error.h"

#endif /* XND_H */
