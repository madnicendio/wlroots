#include <stdlib.h>
#include <spa/utils/result.h>
#include <types/pipewire.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

static void handle_stream_state_changed(void *data, enum pw_stream_state old,
		enum pw_stream_state state, const char *error) {
	wlr_log(WLR_DEBUG, "PipeWire stream state %s -> %s",
		pw_stream_state_as_string(old), pw_stream_state_as_string(state));
}

static const struct pw_stream_events stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.state_changed = handle_stream_state_changed,
};

struct pipewire_output_stream *create_pipewire_output_stream(
		struct wl_event_loop *event_loop, struct wlr_output *output) {
	struct pipewire_output_stream *stream = calloc(1, sizeof(*stream));
	if (stream == NULL) {
		return NULL;
	}

	stream->output = output;

	stream->pipewire = create_pipewire_context(event_loop);
	if (stream->pipewire == NULL) {
		goto error_stream;
	}

	struct pw_properties *props = pw_properties_new(
		PW_KEY_MEDIA_CLASS, "Video/Source",
		PW_KEY_NODE_NAME, "wlroots-remote-desktop",
		PW_KEY_NODE_DESCRIPTION, output->name,
		NULL);
	if (props == NULL) {
		goto error_context;
	}

	// Takes ownership of props
	stream->stream = pw_stream_new(stream->pipewire->core, output->name, props);
	if (stream->stream == NULL) {
		wlr_log(WLR_ERROR, "Failed to create PipeWire stream");
		goto error_context;
	}
	pw_stream_add_listener(stream->stream, &stream->stream_listener,
		&stream_events, stream);

	// TODO: offer a format
	int result = pw_stream_connect(stream->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
		PW_STREAM_FLAG_DRIVER | PW_STREAM_FLAG_MAP_BUFFERS, NULL, 0);
	if (result < 0) {
		wlr_log(WLR_ERROR, "Failed to connect PipeWire stream: %s",
			spa_strerror(result));
		goto error_pw_stream;
	}

	return stream;

error_pw_stream:
	spa_hook_remove(&stream->stream_listener);
	pw_stream_destroy(stream->stream);
error_context:
	destroy_pipewire_context(stream->pipewire);
error_stream:
	free(stream);
	return NULL;
}

void destroy_pipewire_output_stream(struct pipewire_output_stream *stream) {
	if (stream == NULL) {
		return;
	}

	spa_hook_remove(&stream->stream_listener);
	pw_stream_destroy(stream->stream);
	destroy_pipewire_context(stream->pipewire);

	free(stream);
}
