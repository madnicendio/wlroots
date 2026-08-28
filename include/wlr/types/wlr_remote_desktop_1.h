#ifndef WLR_TYPES_WLR_REMOTE_DESKTOP_1_H
#define WLR_TYPES_WLR_REMOTE_DESKTOP_1_H

#include <wayland-server-core.h>
#include <systemd/sd-bus.h>

struct wlr_remote_desktop_1 {
	struct wl_event_loop *event_loop;
	struct wl_event_source *bus_event_source;
	sd_bus *bus;
};

struct wlr_remote_desktop_1 *wlr_remote_desktop_1_create(struct wl_display *display);

void wlr_remote_desktop_1_destroy(struct wlr_remote_desktop_1 *rd1);

#endif  /* WLR_TYPES_WLR_REMOTE_DESKTOP_1_H */
