#include "weather-config.h"
#include "weather-search.h"
#include <libxfce4util/libxfce4util.h>

#define CFG_GROUP "xfce4-quark-weather-plugin"
#define CFG_PATH  "xfce4-quark-weather-plugin/config"

/* Old config path for one-time migration */
#define OLD_CFG_PATH "weather-plugin/config"

/* We need access to the WeatherPlugin struct fields.
   Since the struct is defined in weather-plugin.c, we use an exact-layout copy. */
typedef struct {
    XfcePanelPlugin *plugin;
    WeatherConfig    cfg;
    /* We only need cfg and plugin; remaining fields are safe to ignore */
} WeatherPluginCompat;

static gchar *get_cfg_path(void) {
    return xfce_resource_save_location(XFCE_RESOURCE_CONFIG, CFG_PATH, TRUE);
}

/* One-time migration: copy old config to new path if new doesn't exist */
static void migrate_old_config(void) {
    gchar *new_path = get_cfg_path();
    if (!new_path) return;

    /* If new config already exists, no migration needed */
    if (g_file_test(new_path, G_FILE_TEST_EXISTS)) {
        g_free(new_path);
        return;
    }

    /* Look for old config */
    gchar *old_path = xfce_resource_save_location(
        XFCE_RESOURCE_CONFIG, OLD_CFG_PATH, FALSE);
    if (old_path && g_file_test(old_path, G_FILE_TEST_EXISTS)) {
        gchar *contents = NULL;
        gsize len = 0;
        if (g_file_get_contents(old_path, &contents, &len, NULL)) {
            /* Rewrite the group name inside the config */
            gchar *migrated = g_regex_replace_literal(
                g_regex_new("\\[weather-plugin\\]", 0, 0, NULL),
                contents, -1, 0,
                "[xfce4-quark-weather-plugin]", 0, NULL);
            if (migrated) {
                g_file_set_contents(new_path, migrated, -1, NULL);
                g_free(migrated);
                g_info("quark-weather: migrated config from %s → %s", old_path, new_path);
            } else {
                /* Fallback: copy as-is */
                g_file_set_contents(new_path, contents, len, NULL);
            }
            g_free(contents);
        }
    }
    g_free(old_path);
    g_free(new_path);
}

void weather_config_load(WeatherConfig *cfg) {
    migrate_old_config();

    gchar     *path = get_cfg_path();
    if (!path) return;
    GKeyFile  *kf   = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        gchar *loc = g_key_file_get_string(kf, CFG_GROUP, "location", NULL);
        if (loc) {
            g_free(cfg->location);
            cfg->location = loc;
        }
        GError *err = NULL;
        gdouble lat = g_key_file_get_double(kf, CFG_GROUP, "lat", &err);
        if (!err) cfg->lat = lat; else g_clear_error(&err);

        gdouble lon = g_key_file_get_double(kf, CFG_GROUP, "lon", &err);
        if (!err) cfg->lon = lon; else g_clear_error(&err);

        gboolean fahr = g_key_file_get_boolean(kf, CFG_GROUP, "fahrenheit", &err);
        if (!err) cfg->use_fahrenheit = fahr; else g_clear_error(&err);

        gint refresh = g_key_file_get_integer(kf, CFG_GROUP, "refresh_min", &err);
        if (!err && refresh > 0) cfg->refresh_minutes = refresh; else g_clear_error(&err);

        gint prov = g_key_file_get_integer(kf, CFG_GROUP, "provider", &err);
        if (!err && prov >= 0 && prov <= 2) cfg->provider = (WeatherProvider)prov; else g_clear_error(&err);

        gchar *api_key = g_key_file_get_string(kf, CFG_GROUP, "owm_api_key", NULL);
        if (api_key) {
            g_free(cfg->owm_api_key);
            cfg->owm_api_key = api_key;
        }
    }
    g_key_file_free(kf);
    g_free(path);
}

void weather_config_save(const WeatherConfig *cfg) {
    gchar    *path = get_cfg_path();
    if (!path) return;
    GKeyFile *kf   = g_key_file_new();
    g_key_file_set_string(kf,  CFG_GROUP, "location",    cfg->location ? cfg->location : "");
    g_key_file_set_double(kf,  CFG_GROUP, "lat",         cfg->lat);
    g_key_file_set_double(kf,  CFG_GROUP, "lon",         cfg->lon);
    g_key_file_set_boolean(kf, CFG_GROUP, "fahrenheit",  cfg->use_fahrenheit);
    g_key_file_set_integer(kf, CFG_GROUP, "refresh_min", cfg->refresh_minutes);
    g_key_file_set_integer(kf, CFG_GROUP, "provider",    cfg->provider);
    if (cfg->owm_api_key)
        g_key_file_set_string(kf, CFG_GROUP, "owm_api_key", cfg->owm_api_key);

    gsize   len;
    gchar *data = g_key_file_to_data(kf, &len, NULL);
    g_file_set_contents(path, data, len, NULL);
    g_free(data);
    g_key_file_free(kf);
    g_free(path);
}

/* Callback for "Change Location…" button in settings dialog */
static void on_change_location_clicked(GtkButton *btn, gpointer ud) {
    (void)ud;
    WeatherConfig *cfg = g_object_get_data(G_OBJECT(btn), "cfg");
    GtkWidget *lbl     = g_object_get_data(G_OBJECT(btn), "lbl-loc");
    GtkWidget *parent  = g_object_get_data(G_OBJECT(btn), "parent");

    weather_search_dialog(cfg, GTK_WINDOW(parent));

    /* Update the label to reflect the new location */
    if (lbl) {
        gchar *txt = g_strdup_printf("%s  (%.4f°, %.4f°)",
            cfg->location ? cfg->location : "Not set",
            cfg->lat, cfg->lon);
        gtk_label_set_text(GTK_LABEL(lbl), txt);
        g_free(txt);
    }
}

void weather_config_dialog(XfcePanelPlugin *plugin, gpointer ud) {
    (void)plugin;
    WeatherPluginCompat *wp = (WeatherPluginCompat *)ud;
    WeatherConfig *cfg = &wp->cfg;

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Quark Weather Settings", NULL,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "_OK", GTK_RESPONSE_OK, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 400, 340);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *grid    = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid),    10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_start(grid, 16);
    gtk_widget_set_margin_end(grid, 16);
    gtk_widget_set_margin_top(grid, 16);
    gtk_widget_set_margin_bottom(grid, 16);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    /* ── Location (read-only display + change button) ────────────── */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Location:"), 0, 0, 1, 1);

    gchar *loc_display = g_strdup_printf("%s  (%.4f°, %.4f°)",
        cfg->location ? cfg->location : "Not set",
        cfg->lat, cfg->lon);
    GtkWidget *lbl_loc = gtk_label_new(loc_display);
    g_free(loc_display);
    gtk_label_set_xalign(GTK_LABEL(lbl_loc), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(lbl_loc), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(lbl_loc, TRUE);
    gtk_grid_attach(GTK_GRID(grid), lbl_loc, 1, 0, 1, 1);

    GtkWidget *btn_loc = gtk_button_new_with_label("Change Location…");
    gtk_grid_attach(GTK_GRID(grid), btn_loc, 2, 0, 1, 1);

    /* Wire the change-location button — store cfg+label reference */
    /* We store these as object data on the button itself */
    g_object_set_data(G_OBJECT(btn_loc), "cfg",     cfg);
    g_object_set_data(G_OBJECT(btn_loc), "lbl-loc", lbl_loc);
    g_object_set_data(G_OBJECT(btn_loc), "parent",  dlg);

    g_signal_connect(btn_loc, "clicked",
        G_CALLBACK(on_change_location_clicked), NULL);

    /* ── Refresh interval ────────────────────────────────────────── */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Refresh (min):"), 0, 1, 1, 1);
    GtkWidget *e_refresh = gtk_spin_button_new_with_range(5, 360, 5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(e_refresh), cfg->refresh_minutes);
    gtk_grid_attach(GTK_GRID(grid), e_refresh, 1, 1, 2, 1);

    /* ── Units toggle ────────────────────────────────────────────── */
    GtkWidget *chk_f = gtk_check_button_new_with_label("Use Fahrenheit");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_f), cfg->use_fahrenheit);
    gtk_grid_attach(GTK_GRID(grid), chk_f, 0, 2, 3, 1);

    /* ── Provider ────────────────────────────────────────────────── */
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Provider:"), 0, 3, 1, 1);
    GtkWidget *combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "met.no (default, free)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "OpenWeatherMap");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "wttr.in");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), cfg->provider);
    gtk_grid_attach(GTK_GRID(grid), combo, 1, 3, 2, 1);

    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        cfg->refresh_minutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(e_refresh));
        cfg->use_fahrenheit  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_f));
        cfg->provider        = (WeatherProvider)gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        weather_config_save(cfg);
    }
    gtk_widget_destroy(dlg);
}
