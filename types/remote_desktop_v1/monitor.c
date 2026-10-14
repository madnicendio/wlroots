#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <types/pipewire.h>
#include <types/wlr_remote_desktop_1.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#define REMOTE_DESKTOP_1_MONITOR_VERSION 1

// Cursor mode values from the RD1 draft. Only METADATA is advertised, and
// it's the only mode OpenPipeWireStream below is aware of.
#define REMOTE_DESKTOP_1_CURSOR_MODE_METADATA 3

// A PipeWire stream opened with Monitor.OpenPipeWireStream. Not a D-Bus
// object of its own, it's addressed by its id within the monitor.
struct wlr_remote_desktop_1_stream {
	struct wl_list link; // wlr_remote_desktop_1_monitor.streams

	char *id;
	struct pipewire_output_stream *pipewire;

	// The OpenPipeWireStream call, replied to once the node is ready
	sd_bus_message *pending_open;

	struct wl_listener pipewire_ready;
};

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

static void reply_open_pipewire_stream(
		struct wlr_remote_desktop_1_stream *stream) {
	sd_bus_message *reply = NULL;
	int result = sd_bus_message_new_method_return(stream->pending_open, &reply);
	if (result >= 0) {
		result = sd_bus_message_append(reply, "s", stream->id);
	}
	if (result >= 0) {
		result = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "{sv}");
	}
	if (result >= 0) {
		result = sd_bus_message_append(reply, "{sv}", "node-id", "u",
			stream->pipewire->node_id);
	}
	if (result >= 0) {
		// TODO: the serial is still a placeholder
		result = sd_bus_message_append(reply, "{sv}", "serial", "t", (uint64_t)0);
	}
	if (result >= 0) {
		result = sd_bus_message_close_container(reply);
	}
	if (result >= 0) {
		result = sd_bus_send(NULL, reply, NULL);
	}
	if (result < 0) {
		wlr_log(WLR_ERROR, "Failed to reply to OpenPipeWireStream: %s",
			strerror(-result));
	}

	sd_bus_message_unref(reply);
	stream->pending_open = sd_bus_message_unref(stream->pending_open);
}

static void destroy_stream(struct wlr_remote_desktop_1_stream *stream) {
	if (stream->pending_open != NULL) {
		sd_bus_reply_method_errorf(stream->pending_open, SD_BUS_ERROR_FAILED,
			"PipeWire stream was closed before it was ready");
		sd_bus_message_unref(stream->pending_open);
	}

	wl_list_remove(&stream->pipewire_ready.link);
	destroy_pipewire_output_stream(stream->pipewire);

	wl_list_remove(&stream->link);
	free(stream->id);
	free(stream);
}

static void stream_handle_pipewire_ready(struct wl_listener *listener,
		void *data) {
	struct wlr_remote_desktop_1_stream *stream =
		wl_container_of(listener, stream, pipewire_ready);
	reply_open_pipewire_stream(stream);
}

static struct wlr_remote_desktop_1_stream *create_stream(
		struct wlr_remote_desktop_1_monitor *monitor) {
	struct wlr_remote_desktop_1_stream *stream = calloc(1, sizeof(*stream));
	if (stream == NULL) {
		return NULL;
	}

	stream->id = remote_desktop_1_format_path("%u",
		monitor->stream_id_counter++);
	if (stream->id == NULL) {
		free(stream);
		return NULL;
	}

	// The PipeWire connection is opened here and lives as long as the stream
	stream->pipewire = create_pipewire_output_stream(
		monitor->session->manager->event_loop, monitor->output);
	if (stream->pipewire == NULL) {
		free(stream->id);
		free(stream);
		return NULL;
	}

	stream->pipewire_ready.notify = stream_handle_pipewire_ready;
	wl_signal_add(&stream->pipewire->events.ready, &stream->pipewire_ready);

	wl_list_insert(&monitor->streams, &stream->link);
	return stream;
}

static int handle_open_pipewire_stream(sd_bus_message *message,
		void *userdata, sd_bus_error *error) {
	struct wlr_remote_desktop_1_monitor *monitor = userdata;

	// No-op: @options ("cursor-mode") is accepted but ignored.

	struct wlr_remote_desktop_1_stream *stream = create_stream(monitor);
	if (stream == NULL) {
		return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_FAILED,
			"Failed to open PipeWire stream");
	}

	// The node id and serial are only known once PipeWire has created the
	// node, so the reply is sent from stream_handle_pipewire_ready()
	stream->pending_open = sd_bus_message_ref(message);
	return 1;
}

static int handle_close_pipewire_stream(sd_bus_message *message,
		void *userdata, sd_bus_error *error) {
	struct wlr_remote_desktop_1_monitor *monitor = userdata;

	const char *id;
	int result = sd_bus_message_read(message, "s", &id);
	if (result < 0) {
		return result;
	}

	struct wlr_remote_desktop_1_stream *stream;
	wl_list_for_each(stream, &monitor->streams, link) {
		if (strcmp(stream->id, id) == 0) {
			destroy_stream(stream);
			return sd_bus_reply_method_return(message, "");
		}
	}

	return sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_ARGS,
		"No PipeWire stream with id \"%s\"", id);
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
	wl_list_init(&monitor->streams);

	uint32_t id = session->monitor_id_counter++;
	monitor->path = remote_desktop_1_format_path("%s/Monitor/%u",
		session->path, id);
	if (monitor->path == NULL) {
		goto error_monitor;
	}

	int result = sd_bus_add_object_vtable(session->manager->bus,
		&monitor->iface_slot, monitor->path, REMOTE_DESKTOP_1_MONITOR_INTERFACE,
		remote_desktop_1_monitor_vtable, monitor);
	if (result < 0) {
		wlr_log(WLR_ERROR,
			"Failed to register RemoteDesktop1.Monitor object: %s",
			strerror(-result));
		goto error_path;
	}

	monitor->backend = wlr_headless_backend_create(session->manager->event_loop);
	if (monitor->backend == NULL) {
		wlr_log(WLR_ERROR, "Failed to create headless backend for monitor");
		goto error_vtable;
	}

	// Add backend to the compositor's multi backend, so the output shows up
	// through the compositor's regular new_output handler
	if (!wlr_multi_backend_add(session->manager->backend, monitor->backend)) {
		wlr_log(WLR_ERROR, "Failed to add headless backend for monitor");
		goto error_backend;
	}

	if (!wlr_backend_start(monitor->backend)) {
		wlr_log(WLR_ERROR, "Failed to start headless backend for monitor");
		goto error_backend;
	}

	// TODO: the height and width are placeholder values, they should come
	// from the client and be sent through the PipeWire-stream and not as
	// an argument to CreateVirtualMonitor
	monitor->output = wlr_headless_add_output(monitor->backend, 1280, 720);
	if (monitor->output == NULL) {
		wlr_log(WLR_ERROR, "Failed to create headless output for monitor");
		goto error_backend;
	}

	// To clean up single monitors, or all of them when a session is ended
	wl_list_insert(&session->monitors, &monitor->link);

	// "Listing monitors is done via the org.freedesktop.DBus.ObjectManager
	// interface implemented on the same path as the session." This notifies
	// the session's ObjectManager that a new child object appeared.
	sd_bus_emit_object_added(session->manager->bus, monitor->path);

	return monitor;

error_backend:
	wlr_backend_destroy(monitor->backend);
error_vtable:
	sd_bus_slot_unref(monitor->iface_slot);
error_path:
	free(monitor->path);
error_monitor:
	free(monitor);
	return NULL;
}

void destroy_remote_desktop_1_monitor(
		struct wlr_remote_desktop_1_monitor *monitor) {
	if (monitor == NULL) {
		return;
	}

	sd_bus_emit_object_removed(monitor->session->manager->bus, monitor->path);

	// Before the backend, since the streams use its output
	struct wlr_remote_desktop_1_stream *stream, *tmp;
	wl_list_for_each_safe(stream, tmp, &monitor->streams, link) {
		destroy_stream(stream);
	}

	// Destroys the output too, which the compositor sees through
	// wlr_output.events.destroy.
	wlr_backend_destroy(monitor->backend);

	sd_bus_slot_unref(monitor->iface_slot);
	wl_list_remove(&monitor->link);
	free(monitor->path);
	free(monitor);
}
