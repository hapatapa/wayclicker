#include <gtk/gtk.h>

#include "wayclicker-app.h"
#include "mainwin.h"

struct _WayClickerApp
{
	GtkApplication parent;
};

G_DEFINE_TYPE(WayClickerApp, wayclicker_app, GTK_TYPE_APPLICATION);

static void wayclicker_app_init(WayClickerApp* /*app*/)
{
}

/**
 * Opens up main window.
 */
static void wayclicker_app_activate(GApplication *app)
{
	MainAppWindow *win = main_app_window_new(WAYCLICKER_APP(app));
	gtk_window_present(GTK_WINDOW(win));
}


static void wayclicker_app_class_init(WayClickerAppClass *class)
{
	G_APPLICATION_CLASS(class)->activate = wayclicker_app_activate;
}

WayClickerApp *wayclicker_app_new()
{
	return g_object_new(WAYCLICKER_APP_TYPE, NULL);
}
