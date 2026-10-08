#include <stdlib.h>
#include <spa/utils/result.h>
#include <types/pipewire.h>
#include <wlr/util/log.h>

static void handle_core_error(void *data, uint32_t id, int seq, int res,
		const char *message) {
	wlr_log(WLR_ERROR, "PipeWire core error: id %u seq %d: %s (%s)",
		id, seq, message, spa_strerror(res));
}

static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.error = handle_core_error,
};

static int handle_loop_fd(int fd, uint32_t mask, void *data) {
	struct pipewire_context *pipewire = data;

	// Dispatch everything that is pending without blocking, so PipeWire
	// callbacks run on the compositor's event loop.
	int result = pw_loop_iterate(pipewire->loop, 0);
	if (result < 0) {
		wlr_log(WLR_ERROR, "pw_loop_iterate() failed: %s", spa_strerror(result));
	}

	return 0;
}

struct pipewire_context *create_pipewire_context(
		struct wl_event_loop *event_loop) {
	struct pipewire_context *pipewire = calloc(1, sizeof(*pipewire));
	if (pipewire == NULL) {
		return NULL;
	}

	pw_init(NULL, NULL);

	pipewire->loop = pw_loop_new(NULL);
	if (pipewire->loop == NULL) {
		wlr_log(WLR_ERROR, "Failed to create PipeWire loop");
		goto error_init;
	}
	pw_loop_enter(pipewire->loop);

	pipewire->context = pw_context_new(pipewire->loop, NULL, 0);
	if (pipewire->context == NULL) {
		wlr_log(WLR_ERROR, "Failed to create PipeWire context");
		goto error_loop;
	}

	pipewire->core = pw_context_connect(pipewire->context, NULL, 0);
	if (pipewire->core == NULL) {
		wlr_log(WLR_ERROR, "Failed to connect to PipeWire");
		goto error_context;
	}
	pw_core_add_listener(pipewire->core, &pipewire->core_listener,
		&core_events, pipewire);

	pipewire->loop_event_source = wl_event_loop_add_fd(event_loop,
		pw_loop_get_fd(pipewire->loop), WL_EVENT_READABLE, handle_loop_fd,
		pipewire);
	if (pipewire->loop_event_source == NULL) {
		wlr_log(WLR_ERROR, "Failed to add PipeWire loop to event loop");
		goto error_core;
	}

	return pipewire;

error_core:
	spa_hook_remove(&pipewire->core_listener);
	pw_core_disconnect(pipewire->core);
error_context:
	pw_context_destroy(pipewire->context);
error_loop:
	pw_loop_leave(pipewire->loop);
	pw_loop_destroy(pipewire->loop);
error_init:
	pw_deinit();
	free(pipewire);
	return NULL;
}

void destroy_pipewire_context(struct pipewire_context *pipewire) {
	if (pipewire == NULL) {
		return;
	}

	wl_event_source_remove(pipewire->loop_event_source);

	spa_hook_remove(&pipewire->core_listener);
	pw_core_disconnect(pipewire->core);
	pw_context_destroy(pipewire->context);

	pw_loop_leave(pipewire->loop);
	pw_loop_destroy(pipewire->loop);

	pw_deinit();
	free(pipewire);
}
