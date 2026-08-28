#include <stdlib.h>
#include <wlr/types/wlr_remote_desktop_1.h>
#include <wlr/util/log.h>

static int handle_bus_fd(int fd, uint32_t mask, void *data) {
	struct wlr_remote_desktop_1 *rd1 = data;
	while (sd_bus_process(rd1->bus, NULL) < 0) {
		if (errno == EAGAIN) {
			return 1;
		}
	}
	return 0;
}

static int handle_ping_method(sd_bus_message *message, void *userdata, sd_bus_error *error) {
	wlr_log(WLR_INFO, "D-Bus Ping method invokend!");
	return sd_bus_reply_method_return(message, "s", "Pong from wlroots module!");
}

static const sd_bus_vtable rd1_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Ping", NULL, "s", handle_ping_method, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

struct wlr_remote_desktop_1 *wlr_remote_desktop_1_create(struct wl_display *display) {
	struct wlr_remote_desktop_1 *rd1 = calloc(1, sizeof(*rd1));
	if (rd1 == NULL) {
		return NULL;
	}

	rd1->event_loop = wl_display_get_event_loop(display);

	int result = sd_bus_open_user(&rd1->bus);
	if (result < 0) {
		wlr_log(WLR_ERROR, "Failed to open systemd bus: %s", strerror(-result));
		free(rd1);
		return NULL;
	}

	result = sd_bus_add_object_vtable(
		rd1->bus,
		NULL,
		"/org/freedesktop/RemoteDesktop1",
		"org.freedesktop.RemoteDesktop1",
		rd1_vtable,
		rd1
	);

	if (result < 0) {
		wlr_log(WLR_ERROR, "Failed to register RemoteDesktop1 object: %s", strerror(-result));
		sd_bus_close(rd1->bus);
		free(rd1);
		return NULL;
	}

	result = sd_bus_request_name(rd1->bus, "org.freedesktop.RemoteDesktop1", 0);
	if (result < 0) {
		wlr_log(WLR_ERROR, "Failed to request RemoteDesktop1 name: %s", strerror(-result));
		sd_bus_close(rd1->bus);
		free(rd1);
		return NULL;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	rd1->bus_event_source = wl_event_loop_add_fd(
		loop,
		sd_bus_get_fd(rd1->bus),
		WL_EVENT_READABLE,
		handle_bus_fd,
		rd1
	);

	return rd1;
}

void wlr_remote_desktop_1_destroy(struct wlr_remote_desktop_1 *rd1) {
	if (rd1 == NULL) {
		return;
	}

	if (rd1->bus_event_source != NULL) {
		wl_event_source_remove(rd1->bus_event_source);
	}

	if (rd1->bus != NULL) {
		sd_bus_close(rd1->bus);
		sd_bus_unref(rd1->bus);
	}
	free(rd1);
}
