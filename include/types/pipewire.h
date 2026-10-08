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

#endif  /* TYPES_PIPEWIRE_H */
