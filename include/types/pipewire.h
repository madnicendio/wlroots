#ifndef TYPES_PIPEWIRE_H
#define TYPES_PIPEWIRE_H

#include <pipewire/pipewire.h>
#include <wayland-server-core.h>

// A connection to the PipeWire daemon, driven by the compositor's event loop.
// Knows nothing about D-Bus or RemoteDesktop1. Each stream owns one and creates
// its pw_stream on top of its core.
struct pipewire_context {
	struct pw_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
	struct spa_hook core_listener;

	struct wl_event_source *loop_event_source;
};

struct pipewire_context *create_pipewire_context(
	struct wl_event_loop *event_loop);
void destroy_pipewire_context(struct pipewire_context *pipewire);

struct wlr_output;

// A Video/Source node producing the content of a wlr_output. Owns its own
// pipewire_context, so the PipeWire connection lives exactly as long as the
// stream.
struct pipewire_output_stream {
	struct wlr_output *output;

	struct pipewire_context *pipewire;
	struct pw_stream *stream;
	struct spa_hook stream_listener;

	// Valid once the ready event has been emitted
	uint32_t node_id;

	struct {
		// The node exists in the PipeWire graph and node_id is set.
		// Emitted from a PipeWire callback, listeners must not destroy the
		// stream.
		struct wl_signal ready;
	} events;

	bool is_ready;
};

struct pipewire_output_stream *create_pipewire_output_stream(
	struct wl_event_loop *event_loop, struct wlr_output *output);
void destroy_pipewire_output_stream(struct pipewire_output_stream *stream);

#endif  /* TYPES_PIPEWIRE_H */
