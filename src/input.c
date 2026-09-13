#include <gtk/gtk.h>

#include "input.h"
#include "waylandinput.h"
#include "x11api.h"
#include "config.h"

struct Input
{
    struct WaylandInput *wl; /* may be NULL */
    Display *xdisplay;       /* may be NULL */
    gboolean use_xevent;
};

struct Input *input_open(void)
{
    struct Input *in = g_malloc0(sizeof(*in));

#ifdef HAVE_WAYLAND
    in->wl = wayland_input_open();
#endif

    in->xdisplay = get_display();
    in->use_xevent = config->use_xevent;

    if (!in->wl && !in->xdisplay)
    {
        g_free(in);
        return NULL;
    }

    return in;
}

void input_close(struct Input *in)
{
    if (!in)
        return;

    if (in->wl)
        wayland_input_close(in->wl);
    if (in->xdisplay)
        XCloseDisplay(in->xdisplay);

    g_free(in);
}

gboolean input_uses_wayland(struct Input *in)
{
    return in != NULL && in->wl != NULL;
}

int input_move_to(struct Input *in, int x, int y)
{
    if (!in)
        return FALSE;

    if (in->wl && wayland_input_move_to(in->wl, x, y))
        return TRUE;

    if (in->xdisplay)
    {
        move_to(in->xdisplay, x, y);
        return TRUE;
    }

    return FALSE;
}

int input_mouse_event(struct Input *in, int button, enum MouseEvents event_type)
{
    if (!in)
        return FALSE;

    if (in->wl && wayland_input_mouse_event(in->wl, button, event_type))
        return TRUE;

    if (in->xdisplay)
        return mouse_event(in->xdisplay, button,
                           in->use_xevent ? CLICK_MODE_XEVENT : CLICK_MODE_XTEST,
                           event_type);

    return FALSE;
}

int input_click(struct Input *in, int button, int hold_us)
{
    if (!in)
        return FALSE;

    if (in->wl && wayland_input_click(in->wl, button, hold_us))
        return TRUE;

    if (in->xdisplay)
        return click(in->xdisplay, button,
                     in->use_xevent ? CLICK_MODE_XEVENT : CLICK_MODE_XTEST,
                     hold_us);

    return FALSE;
}

int input_get_cursor(struct Input *in, int *x, int *y)
{
    if (!in)
        return FALSE;

    if (in->wl && wayland_input_get_cursor(in->wl, x, y))
        return TRUE;

    if (in->xdisplay)
    {
        get_cursor_coords(in->xdisplay, x, y);
        return TRUE;
    }

    return FALSE;
}