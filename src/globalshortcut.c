#include <glib.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <X11/XKBlib.h>

#include "config.h"
#include "globalshortcut.h"
#include "x11api.h"

#ifdef HAVE_WAYLAND
#include <wayland-client.h>
#include "hyprland-global-shortcuts-v1-client-protocol.h"
#endif

/* X11 event type constants, reused for a uniform hotkey callback API. */
#ifndef KeyPress
#define KeyPress 2
#endif
#ifndef KeyRelease
#define KeyRelease 3
#endif

struct GlobalShortcut
{
    gboolean available;

    /* Wayland objects (created in globalshortcut_start, then only touched
     * from the listener thread). */
#ifdef HAVE_WAYLAND
    struct wl_display *wl_display;
    struct hyprland_global_shortcuts_manager_v1 *manager;
    struct hyprland_global_shortcut_v1 *shortcut;
#endif

    void (*on_event)(int evtype);

    GThread *thread;
    gboolean running;

    /* Key combo last applied with globalshortcut_apply_hotkey(). */
    char *applied_combo;
};

static struct GlobalShortcut state;

#ifdef HAVE_WAYLAND

static void shortcut_pressed(void *data, struct hyprland_global_shortcut_v1 *shortcut,
                             uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
    struct GlobalShortcut *gs = data;
    (void)shortcut;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;
    if (gs->on_event)
    {
        g_debug("globalshortcut: PRESSED event");
        gs->on_event(KeyPress);
    }
}

static void shortcut_released(void *data, struct hyprland_global_shortcut_v1 *shortcut,
                              uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec)
{
    struct GlobalShortcut *gs = data;
    (void)shortcut;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;
    if (gs->on_event)
    {
        g_debug("globalshortcut: RELEASED event");
        gs->on_event(KeyRelease);
    }
}

static const struct hyprland_global_shortcut_v1_listener shortcut_listener = {
    .pressed = shortcut_pressed,
    .released = shortcut_released,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version)
{
    struct GlobalShortcut *gs = data;
    (void)version;

    if (strcmp(interface, hyprland_global_shortcuts_manager_v1_interface.name) == 0)
    {
        gs->manager = wl_registry_bind(registry, name,
                                       &hyprland_global_shortcuts_manager_v1_interface, 1);
        gs->shortcut = hyprland_global_shortcuts_manager_v1_register_shortcut(
            gs->manager, GLOBALSHORTCUT_ID, GLOBALSHORTCUT_APP_ID,
            "Autoclicker start/stop", "");
        hyprland_global_shortcut_v1_add_listener(gs->shortcut, &shortcut_listener, gs);
    }
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
 * Sends a command to the Hyprland socket and reads the reply.
 * Returns a newly allocated string or NULL on failure.
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
 * Listener thread: dispatches Wayland events until told to stop.
 * Uses a poll with a short timeout so stopping never blocks.
 */
static gpointer globalshortcut_thread_main(gpointer user_data)
{
    struct GlobalShortcut *gs = user_data;

    while (gs->running)
    {
        if (wl_display_prepare_read(gs->wl_display) == 0)
        {
            struct pollfd pfd = {
                .fd = wl_display_get_fd(gs->wl_display),
                .events = POLLIN,
            };

            if (poll(&pfd, 1, 50) > 0)
                wl_display_read_events(gs->wl_display);
            else
                wl_display_cancel_read(gs->wl_display);
        }

        wl_display_dispatch_pending(gs->wl_display);
        wl_display_flush(gs->wl_display);
    }

    return NULL;
}

#endif /* HAVE_WAYLAND */

/**
 * Converts an X11 keycode to the token Hyprland understands in a bind.
 * Modifier keys produce the uppercase mod name (e.g. "CTRL"), everything else
 * keeps its keysym name (e.g. "F8"). Falls back to "code:NN" when the name
 * cannot be resolved (e.g. no XWayland running).
 */
static char *keycode_to_hypr_token(int x11_keycode, gboolean *is_modifier)
{
    *is_modifier = FALSE;

    Display *display = get_display();
    if (!display)
        return g_strdup_printf("code:%d", x11_keycode);

    KeySym sym = XkbKeycodeToKeysym(display, x11_keycode, 0, 0);
    const char *name = XKeysymToString(sym);
    XCloseDisplay(display);

    if (!name)
        return g_strdup_printf("code:%d", x11_keycode);

    switch (sym)
    {
    case XK_Control_L:
    case XK_Control_R:
        *is_modifier = TRUE;
        return g_strdup("CTRL");
    case XK_Shift_L:
    case XK_Shift_R:
        *is_modifier = TRUE;
        return g_strdup("SHIFT");
    case XK_Alt_L:
    case XK_Alt_R:
        *is_modifier = TRUE;
        return g_strdup("ALT");
    case XK_Super_L:
    case XK_Super_R:
        *is_modifier = TRUE;
        return g_strdup("SUPER");
    case XK_Meta_L:
    case XK_Meta_R:
        *is_modifier = TRUE;
        return g_strdup("META");
    default:
        return g_strdup(name);
    }
}

gboolean globalshortcut_is_available(void)
{
    return state.available;
}

gboolean globalshortcut_start(void (*on_event)(int evtype))
{
#ifdef HAVE_WAYLAND
    state.on_event = on_event;

    state.wl_display = wl_display_connect(NULL);
    if (!state.wl_display)
    {
        g_warning("Global shortcut: no Wayland display");
        return FALSE;
    }

    struct wl_registry *registry = wl_display_get_registry(state.wl_display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.wl_display);
    wl_display_roundtrip(state.wl_display); /* let register_shortcut take effect */
    wl_registry_destroy(registry);

    if (!state.manager || !state.shortcut)
    {
        g_debug("Global shortcut: hyprland-global-shortcuts-v1 not available, using X11 fallback");
        wl_display_disconnect(state.wl_display);
        state.wl_display = NULL;
        return FALSE;
    }

    state.running = TRUE;
    state.thread = g_thread_new("globalshortcut", globalshortcut_thread_main, &state);
    state.available = TRUE;

    g_message("Native Wayland global shortcut active (%s:%s)",
              GLOBALSHORTCUT_APP_ID, GLOBALSHORTCUT_ID);
    return TRUE;
#else
    (void)on_event;
    return FALSE;
#endif
}

void globalshortcut_apply_hotkey(int button1, int button2)
{
    if (!state.available || button2 == 0)
        return;

    gboolean dummy;
    gboolean mod;
    char *key_token = keycode_to_hypr_token(button2, &dummy);
    char *mod_token = NULL;

    char *combo;
    if (button1 != -1)
    {
        mod_token = keycode_to_hypr_token(button1, &mod);
        combo = g_strdup_printf("%s + %s", mod_token, key_token);
        g_free(mod_token);
    }
    else
    {
        combo = g_strdup(key_token);
    }
    g_free(key_token);

    /* Remove a stale runtime bind (from a previous hotkey or a previous run)
     * and any duplicates, then bind the current combo. */
    GString *eval = g_string_new("eval ");

    if (state.applied_combo && strcmp(state.applied_combo, combo) != 0)
        g_string_append_printf(eval, "hl.unbind(\"%s\"); ", state.applied_combo);

    g_string_append_printf(eval, "hl.unbind(\"%s\"); ", combo);
    g_string_append_printf(eval, "hl.bind(\"%s\", hl.dsp.global(\"%s:%s\"))",
                           combo, GLOBALSHORTCUT_APP_ID, GLOBALSHORTCUT_ID);

    char *reply = hyprctl_request(eval->str);
    g_string_free(eval, TRUE);

    if (!reply)
    {
        g_warning("Global shortcut: failed to apply hotkey '%s'", combo);
        g_free(combo);
        return;
    }

    if (strncmp(reply, "ok", 2) != 0)
        g_warning("Global shortcut: applying hotkey '%s': %s", combo, reply);

    free(reply);

    g_free(state.applied_combo);
    state.applied_combo = combo;
}

void globalshortcut_stop(void)
{
#ifdef HAVE_WAYLAND
    if (state.thread)
    {
        state.running = FALSE;
        g_thread_join(state.thread);
        state.thread = NULL;
    }

    if (state.shortcut)
        hyprland_global_shortcut_v1_destroy(state.shortcut);
    if (state.manager)
        hyprland_global_shortcuts_manager_v1_destroy(state.manager);
    if (state.wl_display)
    {
        wl_display_disconnect(state.wl_display);
        state.wl_display = NULL;
    }
    state.shortcut = NULL;
    state.manager = NULL;
#endif

    if (state.applied_combo)
    {
        GString *eval = g_string_new("eval hl.unbind(\"");
        g_string_append(eval, state.applied_combo);
        g_string_append(eval, "\")");
        char *reply = hyprctl_request(eval->str);
        g_string_free(eval, TRUE);
        free(reply);
        g_free(state.applied_combo);
        state.applied_combo = NULL;
    }

    state.on_event = NULL;
    state.available = FALSE;
}