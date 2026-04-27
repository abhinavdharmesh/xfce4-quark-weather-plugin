#include <libxfce4panel/libxfce4panel.h>
#include <gtk/gtk.h>
#include <gmodule.h>
#include "weather-data.h"
#include "weather-api.h"
#include "weather-ui.h"
#include "weather-config.h"
#include "weather-search.h"

typedef struct {
    XfcePanelPlugin *plugin;
    WeatherConfig    cfg;
    WeatherData     *cached;
    GtkWidget       *button;
    GtkWidget       *icon;
    GtkWidget       *label_temp;
    GtkWidget       *popup;
    guint            timer_id;
    gboolean         fetching;
} WeatherPlugin;

static void weather_data_free(WeatherData *wd) {
    if (!wd) return;
    g_free(wd->current.condition);
    g_free(wd->current.icon_name);
    g_free(wd->current.symbol_code);
    for (int i = 0; i < wd->hourly_count; i++) {
        g_free(wd->hourly[i].icon_name);
        g_free(wd->hourly[i].symbol_code);
    }
    for (int i = 0; i < wd->daily_count; i++) {
        g_free(wd->daily[i].icon_name);
        g_free(wd->daily[i].symbol_code);
    }
    g_free(wd);
}

static void on_sunrise_ready(gpointer ud) {
    WeatherPlugin *wp = ud;
    if (wp->popup && gtk_widget_get_visible(wp->popup) && wp->cached)
        weather_ui_update_popup(wp->popup, wp->cached, &wp->cfg);
}

static void on_data(WeatherData *data, GError *err, gpointer ud);

static void schedule_fetch(WeatherPlugin *wp) {
    if (wp->fetching) return;
    wp->fetching = TRUE;
    weather_api_fetch(&wp->cfg, on_data, wp);
}

static gboolean on_timer(gpointer ud) {
    schedule_fetch(ud);
    return G_SOURCE_CONTINUE;
}

static void update_panel_button(WeatherPlugin *wp) {
    if (!wp->cached) { gtk_label_set_text(GTK_LABEL(wp->label_temp), "--°"); return; }
    WeatherCurrent *c = &wp->cached->current;
    gdouble t = wp->cfg.use_fahrenheit ? c->temp_c * 9.0/5.0 + 32.0 : c->temp_c;
    gchar *txt = g_strdup_printf("%.0f°%s", t, wp->cfg.use_fahrenheit ? "F" : "C");
    gtk_label_set_text(GTK_LABEL(wp->label_temp), txt);
    g_free(txt);
    if (c->icon_name) {
        GtkIconTheme *theme = gtk_icon_theme_get_default();
        if (gtk_icon_theme_has_icon(theme, c->icon_name))
            gtk_image_set_from_icon_name(GTK_IMAGE(wp->icon),
                c->icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    }
}

static void on_data(WeatherData *data, GError *err, gpointer ud) {
    WeatherPlugin *wp = ud;
    wp->fetching = FALSE;
    if (err) {
        if (wp->button) gtk_widget_set_tooltip_text(wp->button, err->message);
        g_warning("quark-weather: %s", err->message);
        return;
    }
    weather_data_free(wp->cached);
    wp->cached = data;
    if (wp->button) gtk_widget_set_tooltip_text(wp->button, NULL);
    update_panel_button(wp);
    if (wp->popup && gtk_widget_get_visible(wp->popup))
        weather_ui_update_popup(wp->popup, wp->cached, &wp->cfg);
    /* Fire sunrise fetch asynchronously — will patch cached and redraw */
    weather_api_fetch_sunrise(wp->cfg.lat, wp->cfg.lon,
                               &wp->cached, on_sunrise_ready, wp);
}

static gboolean on_button_press(GtkWidget *w, GdkEventButton *ev, gpointer ud) {
    (void)w; (void)ev;
    WeatherPlugin *wp = ud;
    if (wp->popup && gtk_widget_get_visible(wp->popup)) {
        gtk_widget_hide(wp->popup); return TRUE;
    }
    if (wp->popup) { gtk_widget_destroy(wp->popup); wp->popup = NULL; }
    wp->popup = weather_ui_create_popup(wp->plugin);
    if (!wp->popup) return TRUE;
    if (wp->cached) weather_ui_update_popup(wp->popup, wp->cached, &wp->cfg);
    else weather_ui_show_error(wp->popup, "Fetching weather…");
    weather_ui_position_popup(wp->popup, wp->plugin);
    gtk_widget_show_all(wp->popup);
    return TRUE;
}

/* ── About dialog ─────────────────────────────────────────────────────── */
static void weather_about_dialog(XfcePanelPlugin *plugin, gpointer ud) {
    (void)plugin; (void)ud;
    const gchar *authors[] = {
        "Abhinav Dharmesh <abhinavdharmesh@dei.ac.in>",
        NULL
    };
    gtk_show_about_dialog(NULL,
        "program-name", "Quark Weather",
        "version",      "1.0.0",
        "comments",     "Modern weather plugin for Xfce\n"
                        "Powered by met.no — no API key needed",
        "website",      "https://github.com/abhinavdharmesh/xfce4-quark-weather-plugin",
        "website-label", "GitHub",
        "authors",      authors,
        "license-type", GTK_LICENSE_GPL_2_0,
        "logo-icon-name", "weather-few-clouds",
        NULL);
}

/* ── Right-click menu: Change Location ────────────────────────────────── */
static void on_menu_change_location(GtkMenuItem *item, gpointer ud) {
    (void)item;
    WeatherPlugin *wp = ud;
    weather_search_dialog(&wp->cfg,
        GTK_WINDOW(gtk_widget_get_toplevel(wp->button)));
    /* After dialog closes, re-fetch with new location */
    weather_data_free(wp->cached);
    wp->cached = NULL;
    update_panel_button(wp);
    schedule_fetch(wp);
}

static void weather_plugin_free(XfcePanelPlugin *plugin, WeatherPlugin *wp) {
    (void)plugin;
    if (wp->timer_id) g_source_remove(wp->timer_id);
    weather_api_cancel_all();
    if (wp->popup) gtk_widget_destroy(wp->popup);
    weather_data_free(wp->cached);
    g_free(wp->cfg.location);
    g_free(wp->cfg.owm_api_key);
    g_free(wp);
}

static void weather_plugin_construct(XfcePanelPlugin *plugin) {
    WeatherPlugin *wp = g_new0(WeatherPlugin, 1);
    wp->plugin = plugin;
    wp->cfg.location        = g_strdup("Agra,IN");
    wp->cfg.lat             = 27.1767;
    wp->cfg.lon             = 78.0081;
    wp->cfg.use_fahrenheit  = FALSE;
    wp->cfg.refresh_minutes = 60;
    wp->cfg.provider        = PROVIDER_METNO;
    weather_config_load(&wp->cfg);
    if (wp->cfg.refresh_minutes < 1) wp->cfg.refresh_minutes = 60;

    wp->button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(wp->button), GTK_RELIEF_NONE);
    gtk_widget_add_events(wp->button, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(wp->button, "button-press-event",
                     G_CALLBACK(on_button_press), wp);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    wp->icon       = gtk_image_new_from_icon_name("weather-few-clouds-symbolic",
                         GTK_ICON_SIZE_LARGE_TOOLBAR);
    wp->label_temp = gtk_label_new("--°");
    gtk_box_pack_start(GTK_BOX(box), wp->icon,       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), wp->label_temp, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(wp->button), box);
    gtk_container_add(GTK_CONTAINER(plugin),     wp->button);
    gtk_widget_show_all(wp->button);

    /* ── Right-click menu ─────────────────────────────────────── */
    xfce_panel_plugin_menu_show_configure(plugin);
    g_signal_connect(plugin, "configure-plugin",
                     G_CALLBACK(weather_config_dialog), wp);

    xfce_panel_plugin_menu_show_about(plugin);
    g_signal_connect(plugin, "about",
                     G_CALLBACK(weather_about_dialog), wp);

    /* Custom menu items */
    GtkWidget *item_refresh = gtk_menu_item_new_with_label("Refresh Now");
    g_signal_connect_swapped(item_refresh, "activate",
        G_CALLBACK(schedule_fetch), wp);
    xfce_panel_plugin_menu_insert_item(plugin, GTK_MENU_ITEM(item_refresh));

    GtkWidget *item_loc = gtk_menu_item_new_with_label("Change Location…");
    g_signal_connect(item_loc, "activate",
        G_CALLBACK(on_menu_change_location), wp);
    xfce_panel_plugin_menu_insert_item(plugin, GTK_MENU_ITEM(item_loc));

    g_signal_connect(plugin, "free-data",
                     G_CALLBACK(weather_plugin_free), wp);

    schedule_fetch(wp);
    wp->timer_id = g_timeout_add_seconds(
        (guint)(wp->cfg.refresh_minutes * 60), on_timer, wp);
}

XFCE_PANEL_PLUGIN_REGISTER(weather_plugin_construct);
