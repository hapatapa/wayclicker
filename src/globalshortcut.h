#ifndef __GLOBalshortcut_H
#define __GLOBalshortcut_H

#include <glib.h>

/**
 * Global hotkey support for Wayland.
 *
 * On Hyprland this uses the native hyprland-global-shortcuts-v1 protocol:
 * the app registers a shortcut ("wayclicker:wayclicker-toggle") and the key combo
 * itself is bound through a `hl.bind(...)` dispatched via the Hyprland socket.
 * Pressing the combo then delivers `pressed`/`released` events back to us.
 *
 * When the protocol is not available the caller falls back to the X11 key
 * listener (see mainwin.c).
 */

/** App id + shortcut id used for the registered global shortcut. */
#define GLOBALSHORTCUT_APP_ID "wayclicker"
#define GLOBALSHORTCUT_ID "wayclicker-toggle"

/**
 * TRUE when the native Hyprland global-shortcuts manager is available
 * and the shortcut has been registered.
 */
gboolean globalshortcut_is_available(void);

/**
 * Sets up the native Wayland listener: connects, registers the shortcut and
 * spawns the listener thread.
 * @param on_event Called on the Wayland thread with an X11-style
 *                 KeyPress/KeyRelease value whenever the hotkey is activated.
 * @return TRUE when the native global-shortcut protocol is active, FALSE when
 *         the caller should fall back to the X11 listener.
 */
gboolean globalshortcut_start(void (*on_event)(int evtype));

/**
 * Applies (or re-applies) the Hyprland keybind for the given hotkey combo,
 * expressed as X11 keycodes (button1 = modifier, -1 for none; button2 = key).
 * The previously applied combo is unbound first so changing the hotkey does
 * not leave stale binds behind.
 */
void globalshortcut_apply_hotkey(int button1, int button2);

/**
 * Unbinds whatever combo globalshortcut_apply_hotkey() last applied and
 * deregisters the shortcut. Safe to call more than once.
 */
void globalshortcut_stop(void);

#endif