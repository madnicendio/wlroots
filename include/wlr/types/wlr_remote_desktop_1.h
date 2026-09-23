#ifndef WLR_TYPES_WLR_REMOTE_DESKTOP_1_H
#define WLR_TYPES_WLR_REMOTE_DESKTOP_1_H

#include <wayland-server-core.h>
#include <systemd/sd-bus.h>

struct wlr_remote_desktop_1 {
	struct wl_event_loop *event_loop;
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

	uint32_t stream_id_counter;
};

struct wlr_remote_desktop_1_clipboard {
	struct wlr_remote_desktop_1_session *session;

	char *path;
	sd_bus_slot *iface_slot;
};

struct wlr_remote_desktop_1 *wlr_remote_desktop_1_create(
	struct wl_display *display);

void wlr_remote_desktop_1_destroy(struct wlr_remote_desktop_1 *rd1);

#endif  /* WLR_TYPES_WLR_REMOTE_DESKTOP_1_H */
