#pragma once
#include <libxfce4panel/libxfce4panel.h>
#include <gtk/gtk.h>
#include "weather-data.h"

GtkWidget *weather_ui_create_popup(XfcePanelPlugin *plugin);
void weather_ui_update_popup(GtkWidget        *win,
                              const WeatherData *d,
                              const WeatherConfig *cfg);
void weather_ui_show_error(GtkWidget *win, const gchar *msg);
void weather_ui_position_popup(GtkWidget *win, XfcePanelPlugin *plugin);
