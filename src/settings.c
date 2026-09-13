#include <gtk/gtk.h>
#include <pwd.h>
#include <X11/keysymdef.h>
#include <dirent.h>
#include <unistd.h>

#include "settings.h"
#include "x11api.h"
#include "mainwin.h"
#include "version.h"
#include "utils.h"
#include "config.h"
#include "globalshortcut.h"

gboolean isChoosingHotkey = FALSE;

static gboolean hasPreKey = FALSE;

struct _items
{
    GtkWidget *buttons_entry;
    GtkWidget *start_button;
    GtkWidget *xevent_switch;
    GtkWidget *reset_preset_button;
} items;

struct set_buttons_entry_struct
{
    char *text;
};

gboolean set_buttons_entry_text(gpointer *data)
{
    struct set_buttons_entry_struct *args = data;
    gtk_entry_set_text(GTK_ENTRY(items.buttons_entry), args->text);
    free(args->text);
    g_free(args);

    return FALSE;
}

gboolean enable_start_button()
{
    gtk_widget_set_sensitive(items.start_button, TRUE);
    return FALSE;
}

gboolean hotkey_finished()
{
    set_start_stop_button_hotkey_text();
    return FALSE;
}

/**
 * Whether the GTK backend is Wayland. On Wayland the keyboard focus belongs to
 * Wayland surfaces, so X11/XWayland grabs/XI events never deliver key presses
 * and hotkey capture must go through GDK instead.
 */
static gboolean is_wayland_session()
{
    GdkDisplay *display = gdk_display_get_default();
    if (display == NULL)
        return FALSE;
    const char *name = gdk_display_get_name(display);
    return name != NULL && g_strrstr(name, "wayland") != NULL;
}

/**
 * Converts a GDK key event into the X11 keycode the rest of the app uses.
 * GDK reports the hardware keycode as an X11 keycode on both the X11 and
 * Wayland backends, so it can be stored directly.
 */
static int keyval_to_x11_keycode(Display *display, guint keyval, guint hardware_keycode)
{
    if (display != NULL)
    {
        int keycode = XKeysymToKeycode(display, keyval);
        if (keycode != 0)
            return keycode;
    }
    return (int)hardware_keycode;
}

/**
 * Keys that are treated as a modifier prefix for a multi-key combo.
 */
static gboolean is_prekey(GdkModifierType /*state*/, guint keyval)
{
    return keyval == XK_Shift_L || keyval == XK_Shift_R || keyval == XK_Alt_L ||
           keyval == XK_Alt_R || keyval == XK_Escape || keyval == XK_Control_L ||
           keyval == XK_Control_R || keyval == XK_ISO_Level3_Shift ||
           keyval == XK_Super_L || keyval == XK_Super_R;
}

/**
 * Native (Wayland) hotkey capture. Mirrors get_hotkeys_handler() but is driven
 * by GDK key events from the focused settings dialog, since X11 recording grabs
 * cannot observe keys on Wayland-native windows.
 */
static gboolean settings_capture_key_press(GtkWidget */*widget*/, GdkEventKey *event, gpointer /*user_data*/)
{
    if (!isChoosingHotkey || event->type != GDK_KEY_PRESS)
        return FALSE;

    guint keyval = event->keyval;

    // Numlock & caps lock is incredibly buggy and causes memory leaks, pointer errors, free errors...
    if (keyval == XK_Num_Lock || keyval == XK_Caps_Lock)
        return TRUE;

    Display *display = get_display();
    const char *key_str = gdk_keyval_name(keyval);
    if (key_str == NULL)
        key_str = "";

    // If prekey, ex shift, ctrl
    if (is_prekey(event->state, keyval))
    {
        config->button1 = keyval_to_x11_keycode(display, keyval, event->hardware_keycode);
        char *text = g_strdup_printf("%s + ", key_str);
        gtk_entry_set_text(GTK_ENTRY(items.buttons_entry), text);
        g_free(text);
        hasPreKey = TRUE;
        return TRUE;
    }

    config->button2 = keyval_to_x11_keycode(display, keyval, event->hardware_keycode);

    if (hasPreKey)
    {
        const char *text = gtk_entry_get_text(GTK_ENTRY(items.buttons_entry));
        char *full_text = g_strdup_printf("%s%s", text, key_str);
        gtk_entry_set_text(GTK_ENTRY(items.buttons_entry), full_text);
        g_free(full_text);
    }
    else
    {
        config->button1 = -1;
        gtk_entry_set_text(GTK_ENTRY(items.buttons_entry), key_str);
    }

    // Text is freed above
    if (display != NULL)
        XCloseDisplay(display);

    g_key_file_set_integer(config_gfile, CFGK_BUTTON_1, config->button1);
    g_key_file_set_integer(config_gfile, CFGK_BUTTON_2, config->button2);

    g_key_file_save_to_file(config_gfile, configpath, NULL);

    // Re-apply when the native Wayland global shortcut is active.
    if (globalshortcut_is_available())
        globalshortcut_apply_hotkey(config->button1, config->button2);

    gtk_widget_set_sensitive(items.start_button, TRUE);
    isChoosingHotkey = FALSE;
    hasPreKey = FALSE;
    set_start_stop_button_hotkey_text();

    return TRUE;
}

/**
 * Gets the keys pressed when setting hotkey.
 */
void get_hotkeys_handler()
{
    Display *display = get_display();
    if (display == NULL)
    {
        g_idle_add(enable_start_button, NULL);
        isChoosingHotkey = FALSE;
        return;
    }

    mask_config(display, MASK_KEYBOARD_PRESS);

    gboolean hasPreKey = FALSE;
    while (1)
    {
        KeyState keyState;
        get_next_key_state(display, &keyState);

        int state = keyState.button;

        // Numlock & caps lock is incredibly buggy and causes memory leaks, pointer errors, free errors...
        if (state == XKeysymToKeycode(display, XK_Num_Lock) || state == XKeysymToKeycode(display, XK_Caps_Lock))
            continue;

        // If prekey, ex shift, ctrl
        if (state == XKeysymToKeycode(display, XK_Shift_L) || state == XKeysymToKeycode(display, XK_Shift_R) || state == XKeysymToKeycode(display, XK_Alt_L) || state == XKeysymToKeycode(display, XK_Alt_R) || state == XKeysymToKeycode(display, XK_Escape) || state == XKeysymToKeycode(display, XK_Control_L) || state == XKeysymToKeycode(display, XK_Control_R) || state == XKeysymToKeycode(display, XK_ISO_Level3_Shift) || state == XKeysymToKeycode(display, XK_Super_L) || state == XKeysymToKeycode(display, XK_Super_R))
        {
            hasPreKey = TRUE;
            config->button1 = state;
            const char *key_str = keycode_to_string(display, state);
            const char *plus = " + ";
            char *text = malloc(1 + strlen(key_str) + strlen(plus));
            sprintf(text, "%s%s", key_str, plus);

            struct set_buttons_entry_struct *user_data = g_malloc0(sizeof(struct set_buttons_entry_struct));
            user_data->text = text;
            g_idle_add(set_buttons_entry_text, user_data);
        }
        else
        {
            config->button2 = state;
            const char *key_str = keycode_to_string(display, state);
            struct set_buttons_entry_struct *user_data = g_malloc0(sizeof(struct set_buttons_entry_struct));

            if (hasPreKey == TRUE)
            {
                const char *buttons_entry_text = gtk_entry_get_text(GTK_ENTRY(items.buttons_entry));
                char *text = malloc(1 + strlen(buttons_entry_text) + strlen(key_str));
                sprintf(text, "%s%s", buttons_entry_text, key_str);
                user_data->text = text;
            }
            else
            {
                config->button1 = -1;
                char *text = (char *)malloc(1 + strlen(key_str));
                strcpy(text, key_str);
                user_data->text = text;
            }
            // Text is freed in the set_buttons_entry_text function
            g_idle_add(set_buttons_entry_text, user_data);
            break;
        }
    }
    XCloseDisplay(display);
    g_idle_add(enable_start_button, NULL);
    g_idle_add(hotkey_finished, NULL);

    g_key_file_set_integer(config_gfile, CFGK_BUTTON_1, config->button1);
    g_key_file_set_integer(config_gfile, CFGK_BUTTON_2, config->button2);

    g_key_file_save_to_file(config_gfile, configpath, NULL);

    // Re-apply when the native Wayland global shortcut is active.
    if (globalshortcut_is_available())
        globalshortcut_apply_hotkey(config->button1, config->button2);

    isChoosingHotkey = FALSE;
}

void safe_mode_changed(GtkSwitch *self, gboolean state)
{
    g_key_file_set_boolean(config_gfile, CFGK_SAFEMODE, state);
    config->safe_mode_enabled = state;

    g_key_file_save_to_file(config_gfile, configpath, NULL);
    // Hack to make the background color not glitch
    gtk_switch_set_active(self, state);
}

void xevent_switch_changed(GtkSwitch *self, gboolean state)
{
    g_key_file_set_boolean(config_gfile, CFGK_USE_XEVENT, state);

    save_and_populate_config();
    gtk_switch_set_active(self, state);
}

void settings_dialog_response(GtkDialog */*dialog*/, gint /*response*/, gpointer /*user_data*/)
{
    gtk_widget_set_sensitive(GTK_WIDGET(items.start_button), TRUE);
    isChoosingHotkey = FALSE;
    hasPreKey = FALSE;
}

void start_button_pressed(GtkButton *self)
{
    isChoosingHotkey = TRUE;
    hasPreKey = FALSE;
    gtk_widget_set_sensitive(GTK_WIDGET(self), FALSE);
    gtk_entry_set_text(GTK_ENTRY(items.buttons_entry), "Press Desired Keys");
    if (is_wayland_session())
        return; // Captured via the dialog's key-press handler instead.
    g_thread_new("get_hotkeys_handler", get_hotkeys_handler, NULL);
}

void reset_preset_button_pressed()
{
    g_key_file_remove_group(config_gfile, PRESET_CATEGORY_CLICK_INTERVAL, NULL);
    g_key_file_remove_group(config_gfile, PRESET_CATEGORY_OPTIONS, NULL);
    g_key_file_remove_group(config_gfile, PRESET_CATEGORY_MORE_OPTIONS, NULL);

    save_and_populate_config();
    mainappwindow_import_config();
}

void settings_dialog_new()
{
    GtkBuilder *builder = gtk_builder_new_from_resource("/res/ui/settings-dialog.ui");
    GtkDialog *dialog = GTK_DIALOG(gtk_builder_get_object(builder, "dialog"));

    config_read_from_file();

    set_window_icon(dialog);

    gtk_builder_add_callback_symbol(builder, "safe_mode_changed", safe_mode_changed);
    gtk_builder_add_callback_symbol(builder, "xevent_switch_changed", xevent_switch_changed);
    gtk_builder_add_callback_symbol(builder, "start_button_pressed", start_button_pressed);
    gtk_builder_add_callback_symbol(builder, "reset_preset_button_pressed", reset_preset_button_pressed);

    gtk_builder_connect_signals(builder, NULL);

    // Load version
    gtk_label_set_text(GTK_LABEL(gtk_builder_get_object(builder, "version_label")), WAYCLICKER_VERSION);

    // Fill struct
    items.buttons_entry = gtk_builder_get_object(builder, "buttons_entry");
    items.start_button = gtk_builder_get_object(builder, "start_button");
    items.xevent_switch = gtk_builder_get_object(builder, "xevent_switch");

    // Load
    gtk_switch_set_active(GTK_SWITCH(gtk_builder_get_object(builder, "safe_mode_switch")), is_safemode());
    gtk_switch_set_active(GTK_SWITCH(items.xevent_switch), config->use_xevent);

    // Load hotkeys
    Display *display = get_display();
    if (display == NULL)
    {
        if (globalshortcut_is_available())
            gtk_entry_set_text(GTK_ENTRY(items.buttons_entry), "Hyprland: wayclicker:wayclicker-toggle");
        else
            gtk_entry_set_text(GTK_ENTRY(items.buttons_entry), "Unavailable (needs XWayland)");
    }
    else if (is_wayland_session() && globalshortcut_is_available())
    {
        gtk_entry_set_text(GTK_ENTRY(items.buttons_entry), "Hyprland: wayclicker:wayclicker-toggle");
        XCloseDisplay(display);
    }
    else
    {
        const char *button_2_key = keycode_to_string(display, config->button2);
        const char *sep = " + ";
        char *hotkeys;

        if (config->button1 != -1)
        {
            const char *button_1_key = keycode_to_string(display, config->button1);
            hotkeys = malloc(1 + strlen(sep) + strlen(button_2_key) + strlen(button_1_key));
            sprintf(hotkeys, "%s%s%s", button_1_key, sep, button_2_key);
        }
        else
        {
            hotkeys = malloc(1 + strlen(button_2_key));
            sprintf(hotkeys, "%s", button_2_key);
        }
        gtk_entry_set_text(GTK_ENTRY(items.buttons_entry), hotkeys);

        free(hotkeys);
        XCloseDisplay(display);
    }

    // Run
    g_signal_connect(dialog, "response", G_CALLBACK(settings_dialog_response), NULL);
    g_signal_connect(GTK_WIDGET(dialog), "key-press-event", G_CALLBACK(settings_capture_key_press), NULL);
    gtk_dialog_run(dialog);
    gtk_widget_destroy(GTK_WIDGET(dialog));
}
