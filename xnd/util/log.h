/* log.h

   Log file routines, exclusive to libxnd.dylib. Other modules report
   errors through util/error.h and do not log.  */
#ifndef XND_LOG_H
#define XND_LOG_H

#include "common/compiler.h"

__BEGIN_DECLS

void xnd_log_setup (void);
void xnd_log_cleanup (void);
void xnd_log_atfork_child (void);

void xnd_log_printf (const char *fmt, ...)
  __attribute_format__ ((__printf__, 1, 2));

__END_DECLS
#endif /* XND_LOG_H */
