/* dispatch_wrappers.h */
#ifndef DISPATCH_WRAPPERS_H
#define DISPATCH_WRAPPERS_H

#include <pthread.h>
#include <dispatch/dispatch.h>
#include "xnd/xnd.h"

#define DISPATCH_WRAPPER_LIST(X) \
	X(dispatch_queue_create) \
	X(dispatch_get_global_queue) \
	X(dispatch_get_main_queue) \
	X(dispatch_main) \
	X(dispatch_set_target_queue) \
	X(dispatch_queue_get_label) \
	X(dispatch_group_create) \
	X(dispatch_group_enter) \
	X(dispatch_group_leave) \
	X(dispatch_group_notify) \
	X(dispatch_group_notify_f) \
	X(dispatch_group_async) \
	X(dispatch_group_async_f) \
	X(dispatch_group_wait)

dispatch_queue_t dispatch_queue_create_hook(const char *,
					    dispatch_queue_attr_t);
dispatch_queue_t dispatch_get_global_queue_hook(long, unsigned long);
dispatch_queue_t dispatch_get_main_queue_hook(void);
void dispatch_main_hook(void);
void dispatch_set_target_queue_hook(dispatch_object_t, dispatch_queue_t);
const char *dispatch_queue_get_label_hook(dispatch_queue_t);

dispatch_group_t dispatch_group_create_hook(void);
void dispatch_group_enter_hook(dispatch_group_t);
void dispatch_group_leave_hook(dispatch_group_t);
void dispatch_group_notify_hook(dispatch_group_t, dispatch_queue_t,
				void (^)(void));
void dispatch_group_notify_f_hook(dispatch_group_t, dispatch_queue_t,
				  void *, void (*)(void *));
void dispatch_group_async_hook(dispatch_group_t, dispatch_queue_t,
			       void (^)(void));
void dispatch_group_async_f_hook(dispatch_group_t, dispatch_queue_t,
				 void *, void (*)(void *));
long dispatch_group_wait_hook(dispatch_group_t, dispatch_time_t);

void dispatch_async_hook(dispatch_queue_t, void (^)(void));
void dispatch_async_f_hook(dispatch_queue_t, void *, void (*)(void *));
void dispatch_sync_hook(dispatch_queue_t, void (^)(void));
void dispatch_sync_f_hook(dispatch_queue_t, void *, void (*)(void *));

void dispatch_apply_hook(size_t, dispatch_queue_t, void (^)(size_t));
void dispatch_apply_f_hook(size_t, dispatch_queue_t, void *,
			   void (*)(void *, size_t));

void dispatch_after_hook(dispatch_time_t, dispatch_queue_t,
			 void (^)(void));
void dispatch_ater_f_hook(dispatch_time_t, dispatch_queue_t, void *,
			  void (*)(void *));

#endif /* DISPATCH_WRAPPERS_H */
