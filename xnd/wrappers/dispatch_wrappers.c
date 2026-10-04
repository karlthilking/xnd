/* dispatch_wrappers.c */
#include <dispatch/dispatch.h>

#include "xnd/xnd.h"
#include "xnd/interpose.h"
#include "dispatch_wrappers.h"

dispatch_queue_t
dispatch_queue_create_hook(const char *label, dispatch_queue_attr_t attr)
{
	xnd_printf("dispatch_queue_create: %s\n", label);
	return dispatch_queue_create(label, attr);
}

dispatch_queue_t
dispatch_get_global_queue_hook(long priority, unsigned long flags)
{
	const char *label;
	dispatch_queue_t queue;

	queue = dispatch_get_global_queue(priority, flags);
	label = dispatch_queue_get_label(queue);
	xnd_printf("dispatch_get_global_queue: %s\n", label);

	return queue;
}

dispatch_queue_t
dispatch_get_main_queue_hook(void)
{
	const char *label;
	dispatch_queue_t queue;

	queue = dispatch_get_main_queue();
	label = dispatch_queue_get_label(queue);
	xnd_printf("dispatch_get_main_queue: %s\n", label);

	return queue;
}

void
dispatch_main_hook(void)
{
	xnd_printf("dispatch_main called\n");
	dispatch_main();
}

void
dispatch_set_target_queue_hook(dispatch_object_t object,
			       dispatch_queue_t target)
{
	const char *label = dispatch_queue_get_label(target);

	xnd_printf("dispatch_set_target_queue: %s\n", label);
	dispatch_set_target_queue(object, target);
}

const char *
dispatch_queue_get_label_hook(dispatch_queue_t queue)
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_queue_get_label: %s\n", label);
	return label;
}

dispatch_group_t
dispatch_group_create_hook(void)
{
	xnd_printf("dispatch_group_create called\n");
	return dispatch_group_create();
}

void
dispatch_group_enter_hook(dispatch_group_t group)
{
	xnd_printf("dispatch_group_enter called\n");
	dispatch_group_enter(group);
}

void
dispatch_group_leave_hook(dispatch_group_t group)
{
	xnd_printf("dispatch_group_leave called\n");
	dispatch_group_leave(group);
}

void
dispatch_group_notify_hook(dispatch_group_t group, dispatch_queue_t queue,
			   void (^block)(void))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_group_notify: %s\n", label);
	return dispatch_group_notify(group, queue, block);
}

void
dispatch_group_notify_f_hook(dispatch_group_t group,
			     dispatch_queue_t queue,
			     void *context, void (*function)(void *))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_group_notify_f: %s\n", label);
	return dispatch_group_notify_f(group, queue, context, function);
}

void
dispatch_group_async_hook(dispatch_group_t group, dispatch_queue_t queue,
			  void (^block)(void))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_group_async: %s\n", label);
	return dispatch_group_async(group, queue, block);
}

void
dispatch_group_async_f_hook(dispatch_group_t group, dispatch_queue_t queue,
			    void *context, void (*function)(void *))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_group_async_f: %s\n", label);
	return dispatch_group_async_f(group, queue, context, function);
}

long
dispatch_group_wait_hook(dispatch_group_t group, dispatch_time_t timeout)
{
	xnd_printf("dispatch_group_wait: timeout=%llu\n", timeout);
	return dispatch_group_wait(group, timeout);
}

void
dispatch_async_hook(dispatch_queue_t queue, void (^block)(void))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_async: %s\n", label);
	dispatch_async(queue, block);
}

void
dispatch_async_f_hook(dispatch_queue_t queue, void *context,
		      void (*function)(void *))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_async_f: %s\n", label);
	dispatch_async_f(queue, context, function);
}

void
dispatch_sync_hook(dispatch_queue_t queue, void (^block)(void))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_sync: %s\n", label);
	dispatch_sync(queue, block);
}

void
dispatch_sync_f_hook(dispatch_queue_t queue, void *context,
		     void (*function)(void *))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_sync_f: %s\n", label);
	dispatch_sync_f(queue, context, function);
}

void
dispatch_apply_hook(size_t iters, dispatch_queue_t queue,
		    void (^block)(size_t))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_apply: %s\n", label);
	dispatch_apply(iters, queue, block);
}

void
dispatch_apply_f_hook(size_t iters, dispatch_queue_t queue, void *context,
		      void (*function)(void *, size_t))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispath_apply_f: %s\n", label);
	dispatch_apply_f(iters, queue, context, function);
}

void
dispatch_after_hook(dispatch_time_t when, dispatch_queue_t queue,
		    void (^block)(void))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispath_after: %s\n", label);
	dispatch_after(when, queue, block);
}

void
dispatch_after_f_hook(dispatch_time_t when, dispatch_queue_t queue,
		      void *context, void (*function)(void *))
{
	const char *label = dispatch_queue_get_label(queue);

	xnd_printf("dispatch_after_f: %s\n", label);
	dispatch_after(when, queue, ^(void) {
		function(context);
	});
}

static const char *
dispatch_source_type_string(dispatch_source_type_t type)
{
#if 1
	if (type == DISPATCH_SOURCE_TYPE_DATA_ADD)
		return "DISPATCH_SOURCE_TYPE_DATA_ADD";
	else if (type == DISPATCH_SOURCE_TYPE_DATA_OR)
		return "DISPATCH_SOURCE_TYPE_DATA_OR";
	else if (type == DISPATCH_SOURCE_TYPE_DATA_REPLACE)
		return "DISPATCH_SOURCE_TYPE_DATA_REPLACE";
	else if (type == DISPATCH_SOURCE_TYPE_MACH_SEND)
		return "DISPATCH_SOURCE_TYPE_MACH_SEND";
	else if (type == DISPATCH_SOURCE_TYPE_MACH_RECV)
		return "DISPATCH_SOURCE_TYPE_MACH_RECV";
	else if (type == DISPATCH_SOURCE_TYPE_MEMORYPRESSURE)
		return "DISPATCH_SOURCE_TYPE_MEMORYPRESSUE";
	else if (type == DISPATCH_SOURCE_TYPE_PROC)
		return "DISPATCH_SOURCE_TYPE_PROC";
	else if (type == DISPATCH_SOURCE_TYPE_READ)
		return "DISPATCH_SOURCE_TYPE_READ";
	else if (type ==  DISPATCH_SOURCE_TYPE_SIGNAL)
		return "DISPATCH_SOURCE_TYPE_SIGNAL";
	else if (type == DISPATCH_SOURCE_TYPE_TIMER)
		return "DISPATCH_SOURCE_TYPE_TIMER";
	else if (type == DISPATCH_SOURCE_TYPE_VNODE)
		return "DISPATCH_SOURCE_TYPE_VNODE";
	else if (type == DISPATCH_SOURCE_TYPE_WRITE)
		return "DISPATCH_SOURCE_TYPE_WRITE";
#else
	switch (type) {
	case DISPATCH_SOURCE_TYPE_DATA_ADD:
		return "DISPATCH_SOURCE_TYPE_DATA_ADD";
	case DISPATCH_SOURCE_TYPE_DATA_OR:
		return "DISPATCH_SOURCE_TYPE_DATA_OR";
	case DISPATCH_SOURCE_TYPE_DATA_REPLACE:
		return "DISPATCH_SOURCE_TYPE_DATA_REPLACE";
	case DISPATCH_SOURCE_TYPE_MACH_SEND:
		return "DISPATCH_SOURCE_TYPE_MACH_SEND";
	case DISPATCH_SOURCE_TYPE_MACH_RECV:
		return "DISPATCH_SOURCE_TYPE_MACH_RECV";
	case DISPATCH_SOURCE_TYPE_MEMORYPRESSURE:
		return "DISPATCH_SOURCE_TYPE_MEMORYPRESSUE";
	case DISPATCH_SOURCE_TYPE_PROC:
		return "DISPATCH_SOURCE_TYPE_PROC";
	case DISPATCH_SOURCE_TYPE_READ:
		return "DISPATCH_SOURCE_TYPE_READ";
	case DISPATCH_SOURCE_TYPE_SIGNAL:
		return "DISPATCH_SOURCE_TYPE_SIGNAL";
	case DISPATCH_SOURCE_TYPE_TIMER:
		return "DISPATCH_SOURCE_TYPE_TIMER";
	case DISPATCH_SOURCE_TYPE_VNODE:
		return "DISPATCH_SOURCE_TYPE_VNODE";
	case DISPATCH_SOURCE_TYPE_WRITE:
		return "DISPATCH_SOURCE_TYPE_WRITE";
	default:
		break;
	}

#endif
	return NULL;
}

dispatch_source_t
dispatch_source_create_hook(dispatch_source_type_t type, uintptr_t handle,
	unsigned long mask, dispatch_queue_t queue)
{
	xnd_printf("dispatch_source_create: %s, %s\n",
		dispatch_source_type_string(type),
		dispatch_queue_get_label(queue));

	return dispatch_source_create(type, handle, mask, queue);
}

void
dispatch_source_set_timer_hook(dispatch_source_t source,
	dispatch_time_t start, u64 interval, u64 leeway)
{
	xnd_printf("dispatch_source_set_timer\n");

	return dispatch_source_set_timer(source, start, interval, leeway);
}

INTERPOSE(dispatch_queue_create_hook, dispatch_queue_create);
INTERPOSE(dispatch_get_global_queue_hook, dispatch_get_global_queue);
INTERPOSE(dispatch_get_main_queue_hook, dispatch_get_main_queue);
INTERPOSE(dispatch_main_hook, dispatch_main);
INTERPOSE(dispatch_set_target_queue_hook, dispatch_set_target_queue);
INTERPOSE(dispatch_queue_get_label_hook, dispatch_queue_get_label);

INTERPOSE(dispatch_group_create_hook, dispatch_group_create);
INTERPOSE(dispatch_group_enter_hook, dispatch_group_enter);
INTERPOSE(dispatch_group_leave_hook, dispatch_group_leave);
INTERPOSE(dispatch_group_notify_hook, dispatch_group_notify);
INTERPOSE(dispatch_group_notify_f_hook, dispatch_group_notify_f);
INTERPOSE(dispatch_group_async_hook, dispatch_group_async);
INTERPOSE(dispatch_group_async_f_hook, dispatch_group_async_f);
INTERPOSE(dispatch_group_wait_hook, dispatch_group_wait);

INTERPOSE(dispatch_async_hook, dispatch_async);
INTERPOSE(dispatch_async_f_hook, dispatch_async_f);
INTERPOSE(dispatch_sync_hook, dispatch_sync);
INTERPOSE(dispatch_sync_f_hook, dispatch_sync_f);
INTERPOSE(dispatch_apply_hook, dispatch_apply);
INTERPOSE(dispatch_apply_f_hook, dispatch_apply_f);
INTERPOSE(dispatch_after_hook, dispatch_after);
INTERPOSE(dispatch_after_f_hook, dispatch_after_f);

INTERPOSE(dispatch_source_create_hook, dispatch_source_create);
INTERPOSE(dispatch_source_set_timer_hook, dispatch_source_set_timer);
