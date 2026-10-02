#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <types/wlr_remote_desktop_1.h>
#include <wlr/backend/multi.h>
#include <wlr/util/log.h>

#define REMOTE_DESKTOP_1_VERSION 1

char *remote_desktop_1_format_path(const char *fmt, ...) {
	va_list args, args_copy;
	va_start(args, fmt);
	va_copy(args_copy, args);

	int len = vsnprintf(NULL, 0, fmt, args_copy);
	va_end(args_copy);
	if (len < 0) {
		va_end(args);
		return NULL;
	}

	char *path = malloc(len + 1);
	if (path != NULL) {
		vsnprintf(path, len + 1, fmt, args);
	}

	va_end(args);
	return path;
}

int remote_desktop_1_emit_empty_options_signal(sd_bus *bus, const char *path,
		const char *interface, const char *member) {
	sd_bus_message *message = NULL;
	int result = sd_bus_message_new_signal(bus, &message, path, interface, member);
	if (result < 0) {
		return result;
	}

	result = sd_bus_message_open_container(message, SD_BUS_TYPE_ARRAY, "{sv}");
	if (result >= 0) {
		result = sd_bus_message_close_container(message);
	}
	if (result >= 0) {
		result = sd_bus_send(bus, message, NULL);
	}

	sd_bus_message_unref(message);
	return result;
}

static int handle_get_version(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	return sd_bus_message_append(reply, "u", REMOTE_DESKTOP_1_VERSION);
}

static int handle_get_capabilities(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	int result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
	if (result < 0) {
		return result;
	}

	// No-op: persistent sessions/devices are not implemented yet.
	result = sd_bus_message_append(reply, "{sv}", "persistence", "b", false);
	if (result < 0) {
		return result;
	}

	return sd_bus_message_close_container(reply);
}

static int handle_create_session(sd_bus_message *message, void *userdata,
		sd_bus_error *error) {
	struct wlr_remote_desktop_1 *rd1 = userdata;

	// No-op: the options and session_options a{sv} in-args are accepted but
	// ignored, e.g. "persistent" and "takes-control" have no effect.

	struct wlr_remote_desktop_1_session *session =
		create_remote_desktop_1_session(rd1);
	if (session == NULL) {
		return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED,
			"Failed to create RemoteDesktop1 session");
	}

	return sd_bus_reply_method_return(message, "o", session->path);
}

static const sd_bus_vtable remote_desktop_1_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("CreateSession", "a{sv}a{sv}", "o", handle_create_session,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("Version", "u", handle_get_version, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Capabilities", "a{sv}", handle_get_capabilities, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_VTABLE_END,
};

static int handle_bus_fd(int fd, uint32_t mask, void *data) {
	struct wlr_remote_desktop_1 *rd1 = data;

	int result;
	while ((result = sd_bus_process(rd1->bus, NULL)) > 0) {
		// Keep draining queued messages/callbacks.
	}
	if (result < 0) {
		wlr_log(WLR_ERROR, "sd_bus_process() failed: %s", strerror(-result));
	}

	return 0;
}

struct wlr_remote_desktop_1 *wlr_remote_desktop_1_create(
		struct wl_display *display, struct wlr_backend *backend) {
	if (!wlr_backend_is_multi(backend)) {
		wlr_log(WLR_ERROR, "RemoteDesktop1 requires a multi backend");
		return NULL;
	}

	struct wlr_remote_desktop_1 *rd1 = calloc(1, sizeof(*rd1));
	if (rd1 == NULL) {
		return NULL;
	}

	rd1->event_loop = wl_display_get_event_loop(display);
	rd1->backend = backend;
	wl_list_init(&rd1->sessions);

	int result = sd_bus_open_user(&rd1->bus);
	if (result < 0) {
		wlr_log(WLR_ERROR, "Failed to open systemd bus: %s", strerror(-result));
		free(rd1);
		return NULL;
	}

	result = sd_bus_add_object_vtable(rd1->bus, NULL, REMOTE_DESKTOP_1_OBJECT_PATH,
		REMOTE_DESKTOP_1_INTERFACE, remote_desktop_1_vtable, rd1);
	if (result < 0) {
		wlr_log(WLR_ERROR, "Failed to register RemoteDesktop1 object: %s",
			strerror(-result));
		sd_bus_unref(rd1->bus);
		free(rd1);
		return NULL;
	}

	result = sd_bus_request_name(rd1->bus, REMOTE_DESKTOP_1_INTERFACE, 0);
	if (result < 0) {
		wlr_log(WLR_ERROR, "Failed to request RemoteDesktop1 name: %s",
			strerror(-result));
		sd_bus_unref(rd1->bus);
		free(rd1);
		return NULL;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	rd1->bus_event_source = wl_event_loop_add_fd(loop, sd_bus_get_fd(rd1->bus),
		WL_EVENT_READABLE, handle_bus_fd, rd1);

	return rd1;
}

void wlr_remote_desktop_1_destroy(struct wlr_remote_desktop_1 *rd1) {
	if (rd1 == NULL) {
		return;
	}

	struct wlr_remote_desktop_1_session *session, *tmp;
	wl_list_for_each_safe(session, tmp, &rd1->sessions, link) {
		destroy_remote_desktop_1_session(session);
	}

	if (rd1->bus_event_source != NULL) {
		wl_event_source_remove(rd1->bus_event_source);
	}

	if (rd1->bus != NULL) {
		sd_bus_flush_close_unref(rd1->bus);
	}

	free(rd1);
}
