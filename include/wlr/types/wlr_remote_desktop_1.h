#ifndef WLR_TYPES_WLR_REMOTE_DESKTOP_1_H
#define WLR_TYPES_WLR_REMOTE_DESKTOP_1_H

#include <wayland-server-core.h>
#include <systemd/sd-bus.h>

struct wlr_backend;
struct wlr_output;

struct wlr_remote_desktop_1 {
	struct wl_event_loop *event_loop;
	// The compositor's multi backend, virtual monitors are added to it
	struct wlr_backend *backend;
	struct wl_event_source *bus_event_source;
	sd_bus *bus;

	struct wl_list sessions; // wlr_remote_desktop_1_session.link
	uint32_t session_id_counter;
};

struct wlr_remote_desktop_1_session {
	struct wlr_remote_desktop_1 *manager;
	struct wl_list link; // wlr_remote_desktop_1.sessions

	char *path;
	sd_bus_slot *iface_slot;
	sd_bus_slot *object_manager_slot;

	struct wl_list monitors; // wlr_remote_desktop_1_monitor.link
	struct wlr_remote_desktop_1_clipboard *clipboard;

	uint32_t monitor_id_counter;
};

struct wlr_remote_desktop_1_monitor {
	struct wlr_remote_desktop_1_session *session;
	struct wl_list link; // wlr_remote_desktop_1_session.monitors

	char *path;
	sd_bus_slot *iface_slot;

	// Each monitor owns a headless backend with a single output
	// TODO: listen to backend->events.destroy, the compositor destroys it
	// along with its multi backend if that happens before
	// wlr_remote_desktop_1_destroy()
	struct wlr_backend *backend;
	struct wlr_output *output;

	struct wl_list streams; // wlr_remote_desktop_1_stream.link
	uint32_t stream_id_counter;
};

struct wlr_remote_desktop_1_clipboard {
	struct wlr_remote_desktop_1_session *session;

	char *path;
	sd_bus_slot *iface_slot;
};

/**
 * Create the RemoteDesktop1 D-Bus service. The backend must be a multi backend
 * (such as the one returned by wlr_backend_autocreate()), virtual monitors
 * are added to it as headless backends and show up through its new_output
 * event.
 */
struct wlr_remote_desktop_1 *wlr_remote_desktop_1_create(
	struct wl_display *display, struct wlr_backend *backend);

void wlr_remote_desktop_1_destroy(struct wlr_remote_desktop_1 *rd1);

#endif  /* WLR_TYPES_WLR_REMOTE_DESKTOP_1_H */
