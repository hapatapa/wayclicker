#ifndef __WAYCLICKERAPP_H
#define __WAYCLICKERAPP_H

#include <gtk/gtk.h>

#define WAYCLICKER_APP_TYPE (wayclicker_app_get_type())
G_DECLARE_FINAL_TYPE(WayClickerApp, wayclicker_app, WAYCLICKER, APP, GtkApplication)

/**
 * Opens up a new wayclicker app.
 */
WayClickerApp *wayclicker_app_new();

#endif
