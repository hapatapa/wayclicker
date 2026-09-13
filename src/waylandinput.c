#include <glib.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "waylandinput.h"

#ifdef HAVE_WAYLAND
#include <wayland-client.h>
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
#endif

/** Precision used for the normalized absolute motion coordinates. */
#define MOTION_EXTENT 1000000000

struct WaylandInput
{
    gboolean available;

    /* wlr-virtual-pointer objects */
    struct wl_display *wl_display;
    struct zwlr_virtual_pointer_manager_v1 *vp_manager;
    struct zwlr_virtual_pointer_v1 *vp;

    /* Cached desktop layout (logical global coordinates) */
    struct
    {
        gboolean valid;
        double min_x, min_y, width, height;
    } layout;
};

#ifdef HAVE_WAYLAND

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version)
{
    struct WaylandInput *in = data;

    if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0)
        in->vp_manager = wl_registry_bind(registry, name,
                                          &zwlr_virtual_pointer_manager_v1_interface,
                                          MIN(version, 2));
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                  uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/**
 * Talks to the Hyprland compositor socket directly (faster than spawning
 * hyprctl). Returns a newly allocated reply or NULL on any failure.
 */
static char *hyprctl_request(const char *command)
{
    const char *signature = g_getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const char *runtime_dir = g_get_user_runtime_dir();

    if (!signature || !runtime_dir)
        return NULL;

    char *sock_path = g_build_filename(runtime_dir, "hypr", signature, ".socket.sock", NULL);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        g_free(sock_path);
        return NULL;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, sock_path, sizeof(addr.sun_path));
    g_free(sock_path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return NULL;
    }

    if (send(fd, command, strlen(command), 0) < 0)
    {
        close(fd);
        return NULL;
    }

    shutdown(fd, SHUT_WR);

    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = malloc(capacity);

    for (;;)
    {
        if (length + 1 >= capacity)
        {
            capacity *= 2;
            buffer = realloc(buffer, capacity);
        }

        ssize_t n = read(fd, buffer + length, capacity - length - 1);
        if (n <= 0)
            break;

        length += n;
    }

    close(fd);

    if (length == 0)
    {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';
    return buffer;
}

/**
 * Queries the current desktop layout from "j/monitors" and caches the
 * bounding box (in logical global coordinates) used to normalize absolute
 * pointer motion. Returns FALSE when the layout cannot be determined.
 */
static gboolean wayland_input_refresh_layout(struct WaylandInput *in)
{
    char *reply = hyprctl_request("j/monitors");
    if (!reply)
    {
        in->layout.valid = FALSE;
        return FALSE;
    }

    gboolean found = FALSE;
    double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    const char *p = reply;

    while ((p = strstr(p, "{")) != NULL)
    {
        const char *end = strstr(p, "}");
        if (!end)
            break;

        gsize obj_len = end - p;
        char *obj = g_strndup(p, obj_len);

        double mx = 0, my = 0, width = 0, height = 0, scale = 1;
        int has_mx = 0, has_my = 0, has_width = 0, has_height = 0;

#define FIND_NUM(key, var, flag)                                                    \
    do {                                                                            \
        char *k = strstr(obj, key);                                                 \
        if (k)                                                                      \
        {                                                                           \
            char *v = strchr(k + strlen(key), ':');                                 \
            if (v)                                                                  \
            {                                                                       \
                v++;                                                                \
                if (sscanf(v, " %lf", &var) == 1)                                   \
                    flag = 1;                                                       \
            }                                                                       \
        }                                                                           \
    } while (0)

        FIND_NUM("\"x\"", mx, has_mx);
        FIND_NUM("\"y\"", my, has_my);
        FIND_NUM("\"width\"", width, has_width);
        FIND_NUM("\"height\"", height, has_height);
        FIND_NUM("\"scale\"", scale, has_mx); /* scale reuses has_mx to avoid extra var */

#undef FIND_NUM

        g_free(obj);

        if (has_mx && has_my && has_width && has_height)
        {
            double lx = mx / scale;
            double ly = my / scale;
            double lw = width / scale;
            double lh = height / scale;

            if (!found)
            {
                min_x = lx;
                min_y = ly;
                max_x = lx + lw;
                max_y = ly + lh;
                found = TRUE;
            }
            else
            {
                if (lx < min_x)
                    min_x = lx;
                if (ly < min_y)
                    min_y = ly;
                if (lx + lw > max_x)
                    max_x = lx + lw;
                if (ly + lh > max_y)
                    max_y = ly + lh;
            }
        }

        p = end;
    }

    free(reply);

    if (!found)
    {
        in->layout.valid = FALSE;
        return FALSE;
    }

    in->layout.valid = TRUE;
    /* Expand the box a little so cursorpos near the very edge stays in range. */
    in->layout.min_x = min_x;
    in->layout.min_y = min_y;
    in->layout.width = max_x - min_x;
    in->layout.height = max_y - min_y;
    return TRUE;
}

/**
 * Warps the virtual pointer to the given logical global position.
 */
static gboolean wayland_warp(struct WaylandInput *in, double x, double y)
{
    if (!in->vp)
        return FALSE;

    if (!in->layout.valid || x < in->layout.min_x || x > in->layout.min_x + in->layout.width ||
        y < in->layout.min_y || y > in->layout.min_y + in->layout.height)
    {
        if (!wayland_input_refresh_layout(in))
            return FALSE;
        /* Refresh may have left a recently-requested position outside the box. */
        if (x < in->layout.min_x || x > in->layout.min_x + in->layout.width ||
            y < in->layout.min_y || y > in->layout.min_y + in->layout.height)
            return FALSE;
    }

    double nx = (x - in->layout.min_x) / in->layout.width;
    double ny = (y - in->layout.min_y) / in->layout.height;

    /* Round up by 1 extent unit so edge pixels don't get truncated a pixel
     * short by the compositor's own integer truncation. */
    const double eps = 1.0;

    uint32_t time = (uint32_t)(g_get_monotonic_time() / 1000);

    zwlr_virtual_pointer_v1_motion_absolute(in->vp, time,
                                            (uint32_t)(nx * MOTION_EXTENT + 0.5 + eps),
                                            (uint32_t)(ny * MOTION_EXTENT + 0.5 + eps),
                                            MOTION_EXTENT, MOTION_EXTENT);
    zwlr_virtual_pointer_v1_frame(in->vp);
    wl_display_flush(in->wl_display);

    return TRUE;
}

static uint32_t wayland_map_button(int button)
{
    switch (button)
    {
    case 2:
        return BTN_MIDDLE;
    case 3:
        return BTN_RIGHT;
    default:
        return BTN_LEFT;
    }
}

#endif /* HAVE_WAYLAND */

struct WaylandInput *wayland_input_open(void)
{
    struct WaylandInput *in = g_malloc0(sizeof(*in));

#ifdef HAVE_WAYLAND
    in->wl_display = wl_display_connect(NULL);
    if (!in->wl_display)
    {
        g_free(in);
        return NULL;
    }

    struct wl_registry *registry = wl_display_get_registry(in->wl_display);
    wl_registry_add_listener(registry, &registry_listener, in);
    wl_display_roundtrip(in->wl_display);
    wl_registry_destroy(registry);

    if (!in->vp_manager)
    {
        wl_display_disconnect(in->wl_display);
        g_free(in);
        return NULL;
    }

    in->vp = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(in->vp_manager, NULL);
    wl_display_roundtrip(in->wl_display);

    wayland_input_refresh_layout(in);
    in->available = TRUE;
    g_message("Wayland input backend active (wlr-virtual-pointer-unstable-v1)");
#else
    (void)in;
    in->available = FALSE;
#endif

    return in;
}

void wayland_input_close(struct WaylandInput *in)
{
    if (!in)
        return;

#ifdef HAVE_WAYLAND
    if (in->vp)
        zwlr_virtual_pointer_v1_destroy(in->vp);
    if (in->vp_manager)
        zwlr_virtual_pointer_manager_v1_destroy(in->vp_manager);
    if (in->wl_display)
    {
        wl_display_disconnect(in->wl_display);
        in->wl_display = NULL;
    }
#endif

    g_free(in);
}

int wayland_input_move_to(struct WaylandInput *in, int x, int y)
{
#ifdef HAVE_WAYLAND
    if (!in->available)
        return FALSE;
    return wayland_warp(in, x, y);
#else
    return FALSE;
#endif
}

int wayland_input_mouse_event(struct WaylandInput *in, int button, enum MouseEvents event_type)
{
#ifdef HAVE_WAYLAND
    if (!in->available || !in->vp)
        return FALSE;

    /* Place the virtual pointer on top of the real cursor so the button
     * event lands wherever the user-thinks the cursor currently is. */
    int cx, cy;
    if (!wayland_input_get_cursor(in, &cx, &cy))
        return FALSE;

    if (!wayland_warp(in, cx, cy))
        return FALSE;

    uint32_t time = (uint32_t)(g_get_monotonic_time() / 1000);

    zwlr_virtual_pointer_v1_button(in->vp, time, wayland_map_button(button),
                                   (event_type == MOUSE_EVENT_PRESS)
                                       ? WL_POINTER_BUTTON_STATE_PRESSED
                                       : WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(in->vp);
    wl_display_flush(in->wl_display);

    return TRUE;
#else
    return FALSE;
#endif
}

int wayland_input_click(struct WaylandInput *in, int button, int hold_us)
{
    if (!wayland_input_mouse_event(in, button, MOUSE_EVENT_PRESS))
        return FALSE;

    if (hold_us != 0)
        usleep(hold_us);

    return wayland_input_mouse_event(in, button, MOUSE_EVENT_RELEASE);
}

int wayland_input_get_cursor(struct WaylandInput *in, int *x, int *y)
{
    (void)in;
#ifdef HAVE_WAYLAND
    char *reply = hyprctl_request("cursorpos");
    if (!reply)
        return FALSE;

    int nx = 0, ny = 0;
    int matched = sscanf(reply, "%d, %d", &nx, &ny) == 2;
    free(reply);

    if (!matched)
        return FALSE;

    *x = nx;
    *y = ny;
    return TRUE;
#else
    (void)x;
    (void)y;
    return FALSE;
#endif
}