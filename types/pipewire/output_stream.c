#include <stdlib.h>
#include <spa/param/video/format-utils.h>
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

	uint8_t buffer[1024];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

	// BGRx has the same memory layout as DRM_FORMAT_XRGB8888, the headless
	// backend's render format. The size is fixed to the output's for now, and
	// a framerate of 0/1 means frames come whenever the output commits.
	struct spa_video_info_raw info = SPA_VIDEO_INFO_RAW_INIT(
		.format = SPA_VIDEO_FORMAT_BGRx,
		.size = SPA_RECTANGLE(output->width, output->height),
		.framerate = SPA_FRACTION(0, 1));
	const struct spa_pod *params[1];
	params[0] = spa_format_video_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
	if (params[0] == NULL) {
		wlr_log(WLR_ERROR, "Failed to build PipeWire stream format");
		goto error_pw_stream;
	}

	wlr_log(WLR_DEBUG, "Offering PipeWire stream format BGRx %dx%d for %s",
		output->width, output->height, output->name);

	// We drive the graph, since frames are produced when the output
	// commits rather than when the consumer asks for them
	int result = pw_stream_connect(stream->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
		PW_STREAM_FLAG_DRIVER | PW_STREAM_FLAG_MAP_BUFFERS,
		params, 1);
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
