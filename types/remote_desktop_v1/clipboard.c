#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <types/wlr_remote_desktop_1.h>
#include <unistd.h>
#include <wlr/util/log.h>

#define REMOTE_DESKTOP_1_CLIPBOARD_VERSION 1

// Clipboard type values from the RD1 draft. Only CLIPBOARD is advertised.
#define REMOTE_DESKTOP_1_CLIPBOARD_TYPE_CLIPBOARD 1

static int handle_get_version(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	return sd_bus_message_append(reply, "u", REMOTE_DESKTOP_1_CLIPBOARD_VERSION);
}

static int handle_get_supported_types(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	uint32_t types[] = { REMOTE_DESKTOP_1_CLIPBOARD_TYPE_CLIPBOARD };
	return sd_bus_message_append_array(reply, 'u', types, sizeof(types));
}

static int handle_destroy(sd_bus_message *message, void *userdata,
		sd_bus_error *error) {
	struct wlr_remote_desktop_1_clipboard *clipboard = userdata;

	int result = sd_bus_reply_method_return(message, "");
	destroy_remote_desktop_1_clipboard(clipboard);
	return result;
}

static int handle_set_selection(sd_bus_message *message, void *userdata,
		sd_bus_error *error) {
	// No-op: "mime-types" and "clipboard-type" are accepted but nothing is
	// actually claimed, so no SelectionOwnerChanged signal follows.
	return sd_bus_reply_method_return(message, "");
}

static int reply_with_placeholder_fd(sd_bus_message *message) {
	// No-op: hand back one end of a socket pair whose peer is closed
	// immediately. It's a valid fd, but nothing reads or writes real
	// clipboard data through it.
	int fds[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
		return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED,
			"Failed to create placeholder selection socket: %s",
			strerror(errno));
	}
	close(fds[1]);

	int result = sd_bus_reply_method_return(message, "h", fds[0]);
	close(fds[0]);
	return result;
}

static int handle_selection_write(sd_bus_message *message, void *userdata,
		sd_bus_error *error) {
	return reply_with_placeholder_fd(message);
}

static int handle_selection_write_done(sd_bus_message *message,
		void *userdata, sd_bus_error *error) {
	// No-op: "serial"/"success" are accepted but ignored, there is no
	// in-flight transfer state to resolve.
	return sd_bus_reply_method_return(message, "");
}

static int handle_selection_read(sd_bus_message *message, void *userdata,
		sd_bus_error *error) {
	return reply_with_placeholder_fd(message);
}

static const sd_bus_vtable remote_desktop_1_clipboard_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Destroy", "", "", handle_destroy,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetSelection", "a{sv}", "", handle_set_selection,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SelectionWrite", "ua{sv}", "h", handle_selection_write,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SelectionWriteDone", "uba{sv}", "",
		handle_selection_write_done, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SelectionRead", "sa{sv}", "h", handle_selection_read,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("SupportedTypes", "au", handle_get_supported_types, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Version", "u", handle_get_version, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_SIGNAL("SelectionOwnerChanged", "a{sv}", 0),
	SD_BUS_SIGNAL("SelectionTransfer", "sua{sv}", 0),
	SD_BUS_VTABLE_END,
};

struct wlr_remote_desktop_1_clipboard *create_remote_desktop_1_clipboard(
		struct wlr_remote_desktop_1_session *session) {
	struct wlr_remote_desktop_1_clipboard *clipboard =
		calloc(1, sizeof(*clipboard));
	if (clipboard == NULL) {
		return NULL;
	}

	clipboard->session = session;
	clipboard->path = remote_desktop_1_format_path("%s/Clipboard",
		session->path);
	if (clipboard->path == NULL) {
		free(clipboard);
		return NULL;
	}

	int result = sd_bus_add_object_vtable(session->manager->bus,
		&clipboard->iface_slot, clipboard->path,
		REMOTE_DESKTOP_1_CLIPBOARD_INTERFACE,
		remote_desktop_1_clipboard_vtable, clipboard);
	if (result < 0) {
		wlr_log(WLR_ERROR,
			"Failed to register RemoteDesktop1.Clipboard object: %s",
			strerror(-result));
		free(clipboard->path);
		free(clipboard);
		return NULL;
	}

	// Notifies the session's ObjectManager (see session.c) that a new
	// child object appeared.
	sd_bus_emit_object_added(session->manager->bus, clipboard->path);

	return clipboard;
}

void destroy_remote_desktop_1_clipboard(
		struct wlr_remote_desktop_1_clipboard *clipboard) {
	if (clipboard == NULL) {
		return;
	}

	sd_bus_emit_object_removed(clipboard->session->manager->bus,
		clipboard->path);

	if (clipboard->session->clipboard == clipboard) {
		clipboard->session->clipboard = NULL;
	}

	sd_bus_slot_unref(clipboard->iface_slot);
	free(clipboard->path);
	free(clipboard);
}
