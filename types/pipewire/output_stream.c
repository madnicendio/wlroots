#include <drm_fourcc.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <spa/param/video/format-utils.h>
#include <spa/utils/result.h>
#include <types/pipewire.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

static void handle_closed_idle(void *data) {
	struct pipewire_output_stream *stream = data;

	// Idle sources are removed by the event loop once dispatched
	stream->closed_idle = NULL;
	wl_signal_emit_mutable(&stream->events.closed, NULL);
}

static void schedule_closed(struct pipewire_output_stream *stream) {
	if (stream->closed_idle != NULL) {
		return;
	}

	stream->closed_idle = wl_event_loop_add_idle(stream->event_loop,
		handle_closed_idle, stream);
	if (stream->closed_idle == NULL) {
		wlr_log(WLR_ERROR, "Failed to schedule PipeWire stream close");
	}
}

static bool read_serial(struct pipewire_output_stream *stream) {
	// The stream's properties are updated with the ones of the bound node,
	// which includes the serial, before it goes to PAUSED
	const struct pw_properties *props = pw_stream_get_properties(stream->stream);
	const char *serial = pw_properties_get(props, PW_KEY_OBJECT_SERIAL);
	if (serial == NULL) {
		return false;
	}

	char *end;
	stream->serial = strtoull(serial, &end, 10);
	return *serial != '\0' && *end == '\0';
}

static bool copy_output_buffer(struct pipewire_output_stream *stream,
		struct wlr_buffer *src, void *dst) {
	struct wlr_texture *texture =
		wlr_texture_from_buffer(stream->output->renderer, src);
	if (texture == NULL) {
		return false;
	}

	// TODO: only copy the damaged region. That requires tracking the damage
	// since each buffer in the pool was last filled, not since the last
	// frame.
	bool ok = wlr_texture_read_pixels(texture,
		&(struct wlr_texture_read_pixels_options){
			.data = dst,
			.format = DRM_FORMAT_XRGB8888,
			.stride = stream->stride,
		});

	wlr_texture_destroy(texture);
	return ok;
}

static void queue_corrupted_buffer(struct pipewire_output_stream *stream,
		struct pw_buffer *pw_buffer) {
	struct spa_chunk *chunk = pw_buffer->buffer->datas[0].chunk;
	chunk->size = 0;
	chunk->flags = SPA_CHUNK_FLAG_CORRUPTED;
	pw_stream_queue_buffer(stream->stream, pw_buffer);
}

static void handle_output_commit(struct wl_listener *listener, void *data) {
	struct pipewire_output_stream *stream =
		wl_container_of(listener, stream, output_commit);
	struct wlr_output_event_commit *event = data;
	const struct wlr_output_state *state = event->state;

	if (!(state->committed & WLR_OUTPUT_STATE_BUFFER)) {
		return;
	}
	struct wlr_buffer *src = state->buffer;

	if ((uint32_t)src->width != stream->width ||
			(uint32_t)src->height != stream->height) {
		// TODO: renegotiate the size with the consumer
		wlr_log(WLR_DEBUG, "Output is %dx%d but stream is %ux%u, skipping frame",
			src->width, src->height, stream->width, stream->height);
		return;
	}

	struct pw_buffer *pw_buffer = pw_stream_dequeue_buffer(stream->stream);
	if (pw_buffer == NULL) {
		// The consumer still holds every buffer
		return;
	}

	struct spa_buffer *buffer = pw_buffer->buffer;
	struct spa_data *spa_data = &buffer->datas[0];
	uint32_t size = stream->stride * stream->height;
	if (spa_data->data == NULL || spa_data->maxsize < size) {
		wlr_log(WLR_ERROR, "PipeWire buffer is unmapped or too small");
		queue_corrupted_buffer(stream, pw_buffer);
		return;
	}

	if (!copy_output_buffer(stream, src, spa_data->data)) {
		wlr_log(WLR_ERROR, "Failed to copy output buffer to PipeWire buffer");
		queue_corrupted_buffer(stream, pw_buffer);
		return;
	}

	spa_data->chunk->offset = 0;
	spa_data->chunk->size = size;
	spa_data->chunk->stride = stream->stride;
	spa_data->chunk->flags = 0;

	// We're the driver, queueing the buffer is what makes the graph run
	pw_stream_queue_buffer(stream->stream, pw_buffer);
}

static void start_streaming(struct pipewire_output_stream *stream) {
	if (stream->is_streaming) {
		return;
	}
	stream->is_streaming = true;

	// Makes sure every frame is rendered into a buffer we can read, rather
	// than a client buffer being scanned out directly
	wlr_output_lock_attach_render(stream->output, true);

	stream->output_commit.notify = handle_output_commit;
	wl_signal_add(&stream->output->events.commit, &stream->output_commit);
}

static void stop_streaming(struct pipewire_output_stream *stream) {
	if (!stream->is_streaming) {
		return;
	}
	stream->is_streaming = false;

	wl_list_remove(&stream->output_commit.link);
	wlr_output_lock_attach_render(stream->output, false);
}

static void handle_stream_state_changed(void *data, enum pw_stream_state old,
		enum pw_stream_state state, const char *error) {
	struct pipewire_output_stream *stream = data;

	wlr_log(WLR_DEBUG, "PipeWire stream state %s -> %s",
		pw_stream_state_as_string(old), pw_stream_state_as_string(state));

	switch (state) {
	case PW_STREAM_STATE_ERROR:
		wlr_log(WLR_ERROR, "PipeWire stream error: %s", error);
		stop_streaming(stream);
		schedule_closed(stream);
		break;
	case PW_STREAM_STATE_UNCONNECTED:
		stop_streaming(stream);
		schedule_closed(stream);
		break;
	case PW_STREAM_STATE_PAUSED:
		if (stream->is_ready) {
			// The consumer stopped or disconnected
			stop_streaming(stream);
			break;
		}

		// The first PAUSED is when the node has been created in the graph
		stream->node_id = pw_stream_get_node_id(stream->stream);
		if (stream->node_id == SPA_ID_INVALID || !read_serial(stream)) {
			wlr_log(WLR_ERROR, "PipeWire stream has no node id or serial");
			schedule_closed(stream);
			break;
		}

		stream->is_ready = true;
		wlr_log(WLR_DEBUG, "PipeWire stream ready: node id %u, serial %" PRIu64,
			stream->node_id, stream->serial);
		wl_signal_emit_mutable(&stream->events.ready, NULL);
		break;
	case PW_STREAM_STATE_STREAMING:
		start_streaming(stream);
		break;
	case PW_STREAM_STATE_CONNECTING:
		break;
	}
}

static void handle_stream_param_changed(void *data, uint32_t id,
		const struct spa_pod *param) {
	struct pipewire_output_stream *stream = data;

	if (id != SPA_PARAM_Format || param == NULL) {
		return;
	}

	struct spa_video_info_raw info = {0};
	if (spa_format_video_raw_parse(param, &info) < 0) {
		pw_stream_set_error(stream->stream, -EINVAL, "Invalid video format");
		return;
	}

	// BGRx is the only format offered, 4 bytes per pixel with no padding
	stream->width = info.size.width;
	stream->height = info.size.height;
	stream->stride = stream->width * 4;
	wlr_log(WLR_DEBUG, "PipeWire stream format negotiated: %ux%u",
		stream->width, stream->height);

	uint8_t buffer[1024];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	// Shared memory only for now, PipeWire allocates the memfds since the
	// stream isn't created with PW_STREAM_FLAG_ALLOC_BUFFERS
	const struct spa_pod *params[1];
	params[0] = spa_pod_builder_add_object(&builder,
		SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
		SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
		SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
		SPA_PARAM_BUFFERS_size, SPA_POD_Int(stream->stride * stream->height),
		SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stream->stride),
		SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(1 << SPA_DATA_MemFd));

	pw_stream_update_params(stream->stream, params, 1);
}

static const struct pw_stream_events stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.state_changed = handle_stream_state_changed,
	.param_changed = handle_stream_param_changed,
};

struct pipewire_output_stream *create_pipewire_output_stream(
		struct wl_event_loop *event_loop, struct wlr_output *output) {
	struct pipewire_output_stream *stream = calloc(1, sizeof(*stream));
	if (stream == NULL) {
		return NULL;
	}

	stream->output = output;
	stream->event_loop = event_loop;
	stream->node_id = SPA_ID_INVALID;
	wl_signal_init(&stream->events.ready);
	wl_signal_init(&stream->events.closed);

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

	if (stream->closed_idle != NULL) {
		wl_event_source_remove(stream->closed_idle);
	}

	stop_streaming(stream);

	spa_hook_remove(&stream->stream_listener);
	pw_stream_destroy(stream->stream);
	destroy_pipewire_context(stream->pipewire);

	free(stream);
}
