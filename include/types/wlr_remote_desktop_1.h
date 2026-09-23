#ifndef TYPES_WLR_REMOTE_DESKTOP_1_H
#define TYPES_WLR_REMOTE_DESKTOP_1_H

#include <wlr/types/wlr_remote_desktop_1.h>

#define REMOTE_DESKTOP_1_OBJECT_PATH "/org/freedesktop/RemoteDesktop1"
#define REMOTE_DESKTOP_1_INTERFACE "org.freedesktop.RemoteDesktop1"
#define REMOTE_DESKTOP_1_SESSION_INTERFACE "org.freedesktop.RemoteDesktop1.Session"
#define REMOTE_DESKTOP_1_MONITOR_INTERFACE "org.freedesktop.RemoteDesktop1.Monitor"
#define REMOTE_DESKTOP_1_CLIPBOARD_INTERFACE "org.freedesktop.RemoteDesktop1.Clipboard"

// Formats a printf-style string into a newly heap-allocated buffer, e.g. for
// building D-Bus object paths. Returns NULL on failure.
char *remote_desktop_1_format_path(const char *fmt, ...);

// Emits a signal whose sole argument is an empty a{sv} dict, e.g. the
// Removed/Destroyed "options" signals which this prototype never populates.
int remote_desktop_1_emit_empty_options_signal(sd_bus *bus, const char *path,
	const char *interface, const char *member);

struct wlr_remote_desktop_1_session *create_remote_desktop_1_session(
	struct wlr_remote_desktop_1 *manager);
void destroy_remote_desktop_1_session(
	struct wlr_remote_desktop_1_session *session);

struct wlr_remote_desktop_1_monitor *create_remote_desktop_1_monitor(
	struct wlr_remote_desktop_1_session *session);
void destroy_remote_desktop_1_monitor(
	struct wlr_remote_desktop_1_monitor *monitor);

struct wlr_remote_desktop_1_clipboard *create_remote_desktop_1_clipboard(
	struct wlr_remote_desktop_1_session *session);
void destroy_remote_desktop_1_clipboard(
	struct wlr_remote_desktop_1_clipboard *clipboard);

#endif  /* TYPES_WLR_REMOTE_DESKTOP_1_H */
