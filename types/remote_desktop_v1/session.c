#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <types/wlr_remote_desktop_1.h>
#include <unistd.h>
#include <wlr/util/log.h>

#define REMOTE_DESKTOP_1_SESSION_VERSION 1

static int handle_get_version(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	return sd_bus_message_append(reply, "u", REMOTE_DESKTOP_1_SESSION_VERSION);
}

static int handle_get_options(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	// No-op: nothing set via the Options property below is stored, so
	// reading it back always yields an empty dict.
	int result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
	if (result < 0) {
		return result;
	}
	return sd_bus_message_close_container(reply);
}

static int handle_set_options(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *value,
		void *userdata, sd_bus_error *error) {
	// No-op: e.g. "disable-animations" is accepted but has no effect.
	return sd_bus_message_skip(value, "a{sv}");
}

static int handle_get_attributes(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	int result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
	if (result < 0) {
		return result;
	}

	// No-op: "takes-control" is not implemented, so no session ever has
	// control.
	result = sd_bus_message_append(reply, "{sv}", "has-control", "b", false);
	if (result < 0) {
		return result;
	}

	return sd_bus_message_close_container(reply);
}

static int handle_destroy(sd_bus_message *message, void *userdata,
		sd_bus_error *error) {
	struct wlr_remote_desktop_1_session *session = userdata;

	// No-op: the "remove" option is ignored, there is no persistent storage
	// to remove anything from.

	int result = sd_bus_reply_method_return(message, "");
	destroy_remote_desktop_1_session(session);
	return result;
}

static int handle_connect_to_eis(sd_bus_message *message, void *userdata,
		sd_bus_error *error) {
	// No-op: no libei server is wired up. Hand back one end of a socket
	// pair whose peer is closed immediately, so callers get a syntactically
	// valid fd but nothing ever speaks the EIS protocol on it.
	int fds[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
		return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED,
			"Failed to create placeholder EIS socket: %s", strerror(errno));
	}
	close(fds[1]);

	int result = sd_bus_reply_method_return(message, "h", fds[0]);
	close(fds[0]);
	return result;
}

static int handle_create_virtual_monitor(sd_bus_message *message,
		void *userdata, sd_bus_error *error) {
	struct wlr_remote_desktop_1_session *session = userdata;

	// No-op: @options ("persistent", "name") are accepted but ignored.

	struct wlr_remote_desktop_1_monitor *monitor =
		create_remote_desktop_1_monitor(session);
	if (monitor == NULL) {
		return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED,
			"Failed to create RemoteDesktop1 monitor");
	}

	return sd_bus_reply_method_return(message, "o", monitor->path);
}

static int handle_enable_clipboard(sd_bus_message *message, void *userdata,
		sd_bus_error *error) {
	struct wlr_remote_desktop_1_session *session = userdata;

	if (session->clipboard == NULL) {
		session->clipboard = create_remote_desktop_1_clipboard(session);
		if (session->clipboard == NULL) {
			return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED,
				"Failed to create RemoteDesktop1 clipboard");
		}
	}

	return sd_bus_reply_method_return(message, "o", session->clipboard->path);
}

static const sd_bus_vtable remote_desktop_1_session_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Destroy", "a{sv}", "", handle_destroy,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("ConnectToEIS", "a{sv}", "h", handle_connect_to_eis,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("CreateVirtualMonitor", "a{sv}", "o",
		handle_create_virtual_monitor, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("EnableClipboard", "", "o", handle_enable_clipboard,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_WRITABLE_PROPERTY("Options", "a{sv}", handle_get_options,
		handle_set_options, 0, 0),
	SD_BUS_PROPERTY("Attributes", "a{sv}", handle_get_attributes, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Version", "u", handle_get_version, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_SIGNAL("Destroyed", "a{sv}", 0),
	SD_BUS_VTABLE_END,
};

struct wlr_remote_desktop_1_session *create_remote_desktop_1_session(
		struct wlr_remote_desktop_1 *manager) {
	struct wlr_remote_desktop_1_session *session = calloc(1, sizeof(*session));
	if (session == NULL) {
		return NULL;
	}

	session->manager = manager;
	wl_list_init(&session->monitors);

	uint32_t id = manager->session_id_counter++;
	session->path = remote_desktop_1_format_path(
		REMOTE_DESKTOP_1_OBJECT_PATH "/Session/%u", id);
	if (session->path == NULL) {
		free(session);
		return NULL;
	}

	int result = sd_bus_add_object_vtable(manager->bus, &session->iface_slot,
		session->path, REMOTE_DESKTOP_1_SESSION_INTERFACE,
		remote_desktop_1_session_vtable, session);
	if (result < 0) {
		wlr_log(WLR_ERROR,
			"Failed to register RemoteDesktop1.Session object: %s",
			strerror(-result));
		free(session->path);
		free(session);
		return NULL;
	}

	// Makes Monitor and Clipboard child objects created under this session
	// discoverable via org.freedesktop.DBus.ObjectManager on this path.
	result = sd_bus_add_object_manager(manager->bus,
		&session->object_manager_slot, session->path);
	if (result < 0) {
		wlr_log(WLR_ERROR,
			"Failed to register RemoteDesktop1.Session object manager: %s",
			strerror(-result));
		sd_bus_slot_unref(session->iface_slot);
		free(session->path);
		free(session);
		return NULL;
	}

	wl_list_insert(&manager->sessions, &session->link);

	return session;
}

void destroy_remote_desktop_1_session(
		struct wlr_remote_desktop_1_session *session) {
	if (session == NULL) {
		return;
	}

	if (session->clipboard != NULL) {
		destroy_remote_desktop_1_clipboard(session->clipboard);
	}

	struct wlr_remote_desktop_1_monitor *monitor, *tmp;
	wl_list_for_each_safe(monitor, tmp, &session->monitors, link) {
		destroy_remote_desktop_1_monitor(monitor);
	}

	sd_bus_slot_unref(session->object_manager_slot);
	sd_bus_slot_unref(session->iface_slot);

	wl_list_remove(&session->link);
	free(session->path);
	free(session);
}
