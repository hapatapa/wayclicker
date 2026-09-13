#ifndef __WAYLANDINPUT_H
#define __WAYLANDINPUT_H

#include <glib.h>
#include "x11api.h"

/**
 * @brief Opaque handle for a native Wayland input session.
 *
 * The backend simulates input through the wlr-virtual-pointer protocol
 * (zwlr_virtual_pointer_manager_v1) using only the compositor-provided
 * wayland socket. Cursor observation is done through the Hyprland
 * compositor socket when available.
 */
struct WaylandInput;

/**
 * Opens a native Wayland input session.
 * @returns NULL when no usable wayland virtual pointer manager is available.
 */
struct WaylandInput *wayland_input_open(void);

void wayland_input_close(struct WaylandInput *in);

/**
 * Moves the cursor to the given absolute (logical, desktop-global) position.
 */
int wayland_input_move_to(struct WaylandInput *in, int x, int y);

/**
 * Presses/releases the given mouse button (1=left, 2=middle, 3=right) at
 * the current cursor position.
 */
int wayland_input_mouse_event(struct WaylandInput *in, int button, enum MouseEvents event_type);

/**
 * Presses and releases the given mouse button, holding it for hold_us
 * microseconds in between.
 */
int wayland_input_click(struct WaylandInput *in, int button, int hold_us);

/**
 * Gets the current cursor position in logical desktop-global coordinates.
 * @returns FALSE when the position cannot be determined.
 */
int wayland_input_get_cursor(struct WaylandInput *in, int *x, int *y);

#endif