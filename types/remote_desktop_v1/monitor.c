#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <types/wlr_remote_desktop_1.h>
#include <wlr/util/log.h>

#define REMOTE_DESKTOP_1_MONITOR_VERSION 1

// Cursor mode values from the RD1 draft. Only METADATA is advertised, and
// it's the only mode OpenPipeWireStream below is aware of.
#define REMOTE_DESKTOP_1_CURSOR_MODE_METADATA 3

static int handle_get_version(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	return sd_bus_message_append(reply, "u", REMOTE_DESKTOP_1_MONITOR_VERSION);
}

static int handle_get_cursor_modes(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	uint32_t modes[] = { REMOTE_DESKTOP_1_CURSOR_MODE_METADATA };
	return sd_bus_message_append_array(reply, 'u', modes, sizeof(modes));
}

static int handle_get_attributes(sd_bus *bus, const char *path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *userdata, sd_bus_error *error) {
	int result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
	if (result < 0) {
		return result;
	}

	// No-op: every monitor is reported as owned by its session, "name" is
	// left unset since there's no real output behind it.
	result = sd_bus_message_append(reply, "{sv}", "owned", "b", true);
	if (result < 0) {
		return result;
	}

	return sd_bus_message_close_container(reply);
}

static int handle_remove(sd_bus_message *message, void *userdata,
		sd_bus_error *error) {
	struct wlr_remote_desktop_1_monitor *monitor = userdata;

	int result = sd_bus_reply_method_return(message, "");
	if (result < 0) {
		return result;
	}

	remote_desktop_1_emit_empty_options_signal(monitor->session->manager->bus,
		monitor->path, REMOTE_DESKTOP_1_MONITOR_INTERFACE, "Removed");

	destroy_remote_desktop_1_monitor(monitor);
	return 0;
}

static int handle_open_pipewire_stream(sd_bus_message *message,
		void *userdata, sd_bus_error *error) {
	struct wlr_remote_desktop_1_monitor *monitor = userdata;

	// No-op: @options ("cursor-mode") is accepted but ignored. There is no
	// real PipeWire stream behind the returned id/info; node-id and serial
	// are placeholder values.
	char id[16];
	snprintf(id, sizeof(id), "%u", monitor->stream_id_counter++);

	sd_bus_message *reply = NULL;
	int result = sd_bus_message_new_method_return(message, &reply);
	if (result < 0) {
		return result;
	}

	result = sd_bus_message_append(reply, "s", id);
	if (result >= 0) {
		result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
	}
	if (result >= 0) {
		result = sd_bus_message_append(reply, "{sv}", "node-id", "u", (uint32_t)0);
	}
	if (result >= 0) {
		result = sd_bus_message_append(reply, "{sv}", "serial", "t", (uint64_t)0);
	}
	if (result >= 0) {
		result = sd_bus_message_close_container(reply);
	}
	if (result >= 0) {
		result = sd_bus_send(NULL, reply, NULL);
	}

	sd_bus_message_unref(reply);
	return result;
}

static int handle_close_pipewire_stream(sd_bus_message *message,
		void *userdata, sd_bus_error *error) {
	// No-op: the "id" argument is accepted but ignored, there is no stream
	// to actually close.
	return sd_bus_reply_method_return(message, "");
}

static const sd_bus_vtable remote_desktop_1_monitor_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Remove", "", "", handle_remove,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("OpenPipeWireStream", "a{sv}", "sa{sv}",
		handle_open_pipewire_stream, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("ClosePipeWireStream", "s", "",
		handle_close_pipewire_stream, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("CursorModes", "au", handle_get_cursor_modes, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Attributes", "a{sv}", handle_get_attributes, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Version", "u", handle_get_version, 0,
		SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_SIGNAL("Removed", "a{sv}", 0),
	SD_BUS_VTABLE_END,
};

struct wlr_remote_desktop_1_monitor *create_remote_desktop_1_monitor(
		struct wlr_remote_desktop_1_session *session) {
	struct wlr_remote_desktop_1_monitor *monitor = calloc(1, sizeof(*monitor));
	if (monitor == NULL) {
		return NULL;
	}

	monitor->session = session;

	uint32_t id = session->monitor_id_counter++;
	monitor->path = remote_desktop_1_format_path("%s/Monitor/%u",
		session->path, id);
	if (monitor->path == NULL) {
		free(monitor);
		return NULL;
	}

	int result = sd_bus_add_object_vtable(session->manager->bus,
		&monitor->iface_slot, monitor->path, REMOTE_DESKTOP_1_MONITOR_INTERFACE,
		remote_desktop_1_monitor_vtable, monitor);
	if (result < 0) {
		wlr_log(WLR_ERROR,
			"Failed to register RemoteDesktop1.Monitor object: %s",
			strerror(-result));
		free(monitor->path);
		free(monitor);
		return NULL;
	}

	wl_list_insert(&session->monitors, &monitor->link);

	// Notifies the session's ObjectManager (see session.c) that a new
	// child object appeared.
	sd_bus_emit_object_added(session->manager->bus, monitor->path);

	return monitor;
}

void destroy_remote_desktop_1_monitor(
		struct wlr_remote_desktop_1_monitor *monitor) {
	if (monitor == NULL) {
		return;
	}

	sd_bus_emit_object_removed(monitor->session->manager->bus, monitor->path);

	sd_bus_slot_unref(monitor->iface_slot);
	wl_list_remove(&monitor->link);
	free(monitor->path);
	free(monitor);
}
