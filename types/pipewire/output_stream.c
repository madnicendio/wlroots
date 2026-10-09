#include <stdlib.h>
#include <types/pipewire.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

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

	// TODO: create the pw_stream

	return stream;

error_stream:
	free(stream);
	return NULL;
}

void destroy_pipewire_output_stream(struct pipewire_output_stream *stream) {
	if (stream == NULL) {
		return;
	}

	destroy_pipewire_context(stream->pipewire);

	free(stream);
}
