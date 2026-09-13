#ifndef __INPUT_H
#define __INPUT_H

#include <glib.h>
#include "x11api.h"

/**
 * @brief Unified input backend used for input simulation.
 *
 * Prefers a native Wayland backend (wlr-virtual-pointer via the compositor
 * socket) and automatically falls back to the legacy X11 (XTest/XEvent)
 * backend when no usable Wayland virtual pointer manager is available.
 */
struct Input;

/**
 * Opens the best available input backend.
 * @returns NULL when neither Wayland nor X11 input can be used.
 */
struct Input *input_open(void);

void input_close(struct Input *in);

/**
 * @returns TRUE when the Wayland backend is driving the simulation.
 */
gboolean input_uses_wayland(struct Input *in);

/**
 * Moves the cursor to the given absolute desktop position.
 */
int input_move_to(struct Input *in, int x, int y);

/**
 * Presses/releases the given mouse button (1=left, 2=middle, 3=right) at
 * the current cursor position.
 */
int input_mouse_event(struct Input *in, int button, enum MouseEvents event_type);

/**
 * Presses and releases the given mouse button, holding it for hold_us
 * microseconds in between.
 */
int input_click(struct Input *in, int button, int hold_us);

/**
 * Gets the current cursor position in desktop coordinates.
 */
int input_get_cursor(struct Input *in, int *x, int *y);

#endif