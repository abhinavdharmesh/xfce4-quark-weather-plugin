/*
 * weather-search.c — Location search dialog using Nominatim + IP geolocation
 *
 * Provides a modal dialog where the user can:
 *  1. Type a city name and search via Nominatim (OpenStreetMap)
 *  2. Click "Use My Location" for automatic IP-based geolocation
 *
 * On success, updates WeatherConfig (lat, lon, location) and saves.
 */

#include "weather-search.h"
#include "weather-config.h"
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

/* ── Local soup session (independent from weather-api.c) ──────────────── */
static SoupSession *get_search_session(void) {
    static SoupSession *session = NULL;
    if (!session)
        session = soup_session_new_with_options(
            SOUP_SESSION_USER_AGENT,
                "QuarkWeather/1.0 "
                "(Contact: abhinavdharmesh@dei.ac.in; "
                "https://github.com/abhinavdharmesh/xfce4-quark-weather-plugin)",
            SOUP_SESSION_TIMEOUT, 15,
            NULL);
    return session;
}

/* ── Search result entry ─────────────────────────────────────────────── */
typedef struct {
    gchar   *display_name;
    gdouble  lat;
    gdouble  lon;
} SearchResult;

static void search_result_free(SearchResult *r) {
    if (r) { g_free(r->display_name); g_free(r); }
}

/* ── Dialog state ────────────────────────────────────────────────────── */
typedef struct {
    WeatherConfig *cfg;
    GtkWidget     *entry;
    GtkWidget     *listbox;
    GtkWidget     *status_label;
    GtkWidget     *dialog;
    GList         *results;       /* GList of SearchResult* */
    gint           selected_idx;  /* -1 = none */
} SearchState;

static void clear_results(SearchState *st) {
    /* Clear listbox children */
    GList *children = gtk_container_get_children(GTK_CONTAINER(st->listbox));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    /* Free result data */
    g_list_free_full(st->results, (GDestroyNotify)search_result_free);
    st->results = NULL;
    st->selected_idx = -1;
}

/* ── Nominatim search ────────────────────────────────────────────────── */
static void do_nominatim_search(SearchState *st, const gchar *query) {
    clear_results(st);

    if (!query || !*query) {
        gtk_label_set_text(GTK_LABEL(st->status_label), "Type a city name and click Search");
        return;
    }

    gtk_label_set_text(GTK_LABEL(st->status_label), "Searching…");

    /* URL-encode the query */
    gchar *encoded = soup_uri_encode(query, NULL);
    gchar *url = g_strdup_printf(
        "https://nominatim.openstreetmap.org/search"
        "?q=%s&format=json&limit=5&addressdetails=0",
        encoded);
    g_free(encoded);

    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);
    if (!msg) {
        gtk_label_set_text(GTK_LABEL(st->status_label), "Failed to create request");
        return;
    }

    guint status = soup_session_send_message(get_search_session(), msg);

    if (status < 200 || status >= 300) {
        gchar *err = g_strdup_printf("Search failed (HTTP %u)", status);
        gtk_label_set_text(GTK_LABEL(st->status_label), err);
        g_free(err);
        g_object_unref(msg);
        return;
    }

    /* Parse JSON array */
    JsonParser *jp = json_parser_new();
    if (!json_parser_load_from_data(jp,
            msg->response_body->data, msg->response_body->length, NULL)) {
        gtk_label_set_text(GTK_LABEL(st->status_label), "Failed to parse response");
        g_object_unref(jp);
        g_object_unref(msg);
        return;
    }

    JsonNode *root = json_parser_get_root(jp);
    if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
        gtk_label_set_text(GTK_LABEL(st->status_label), "Unexpected response format");
        g_object_unref(jp);
        g_object_unref(msg);
        return;
    }

    JsonArray *arr = json_node_get_array(root);
    guint len = json_array_get_length(arr);

    if (len == 0) {
        gtk_label_set_text(GTK_LABEL(st->status_label), "No results found");
        g_object_unref(jp);
        g_object_unref(msg);
        return;
    }

    for (guint i = 0; i < len; i++) {
        JsonObject *obj = json_array_get_object_element(arr, i);
        if (!obj) continue;

        const gchar *name = NULL;
        if (json_object_has_member(obj, "display_name"))
            name = json_object_get_string_member(obj, "display_name");

        const gchar *lat_s = NULL, *lon_s = NULL;
        if (json_object_has_member(obj, "lat"))
            lat_s = json_object_get_string_member(obj, "lat");
        if (json_object_has_member(obj, "lon"))
            lon_s = json_object_get_string_member(obj, "lon");

        if (!name || !lat_s || !lon_s) continue;

        SearchResult *r = g_new0(SearchResult, 1);
        r->display_name = g_strdup(name);
        r->lat = g_ascii_strtod(lat_s, NULL);
        r->lon = g_ascii_strtod(lon_s, NULL);
        st->results = g_list_append(st->results, r);

        /* Add to listbox */
        GtkWidget *row_label = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(row_label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(row_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(row_label), 60);
        gtk_widget_set_margin_start(row_label, 8);
        gtk_widget_set_margin_end(row_label, 8);
        gtk_widget_set_margin_top(row_label, 4);
        gtk_widget_set_margin_bottom(row_label, 4);
        gtk_list_box_insert(GTK_LIST_BOX(st->listbox), row_label, -1);
    }

    gchar *status_text = g_strdup_printf("%u result%s found", len, len == 1 ? "" : "s");
    gtk_label_set_text(GTK_LABEL(st->status_label), status_text);
    g_free(status_text);

    gtk_widget_show_all(st->listbox);
    g_object_unref(jp);
    g_object_unref(msg);
}

/* ── IP geolocation (ipapi.co) ───────────────────────────────────────── */
static void do_ip_geolocation(SearchState *st) {
    clear_results(st);
    gtk_label_set_text(GTK_LABEL(st->status_label), "Detecting location…");

    SoupMessage *msg = soup_message_new("GET", "https://ipapi.co/json/");
    if (!msg) {
        gtk_label_set_text(GTK_LABEL(st->status_label), "Failed to create request");
        return;
    }

    guint status = soup_session_send_message(get_search_session(), msg);

    if (status < 200 || status >= 300) {
        gchar *err = g_strdup_printf("Geolocation failed (HTTP %u)", status);
        gtk_label_set_text(GTK_LABEL(st->status_label), err);
        g_free(err);
        g_object_unref(msg);
        return;
    }

    JsonParser *jp = json_parser_new();
    if (!json_parser_load_from_data(jp,
            msg->response_body->data, msg->response_body->length, NULL)) {
        gtk_label_set_text(GTK_LABEL(st->status_label), "Failed to parse response");
        g_object_unref(jp);
        g_object_unref(msg);
        return;
    }

    JsonNode *root = json_parser_get_root(jp);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        gtk_label_set_text(GTK_LABEL(st->status_label), "Unexpected response");
        g_object_unref(jp);
        g_object_unref(msg);
        return;
    }

    JsonObject *obj = json_node_get_object(root);
    gdouble lat = 0, lon = 0;
    const gchar *city = NULL, *country = NULL;

    if (json_object_has_member(obj, "latitude"))
        lat = json_object_get_double_member(obj, "latitude");
    if (json_object_has_member(obj, "longitude"))
        lon = json_object_get_double_member(obj, "longitude");
    if (json_object_has_member(obj, "city"))
        city = json_object_get_string_member(obj, "city");
    if (json_object_has_member(obj, "country_name"))
        country = json_object_get_string_member(obj, "country_name");

    if (lat == 0 && lon == 0) {
        gtk_label_set_text(GTK_LABEL(st->status_label),
            "Could not determine location");
        g_object_unref(jp);
        g_object_unref(msg);
        return;
    }

    /* Build display name */
    gchar *name;
    if (city && country)
        name = g_strdup_printf("%s, %s", city, country);
    else if (city)
        name = g_strdup(city);
    else
        name = g_strdup_printf("%.4f, %.4f", lat, lon);

    SearchResult *r = g_new0(SearchResult, 1);
    r->display_name = name;
    r->lat = lat;
    r->lon = lon;
    st->results = g_list_append(st->results, r);

    /* Add to listbox */
    gchar *label_text = g_strdup_printf("📍  %s", name);
    GtkWidget *row_label = gtk_label_new(label_text);
    g_free(label_text);
    gtk_label_set_xalign(GTK_LABEL(row_label), 0.0f);
    gtk_widget_set_margin_start(row_label, 8);
    gtk_widget_set_margin_end(row_label, 8);
    gtk_widget_set_margin_top(row_label, 4);
    gtk_widget_set_margin_bottom(row_label, 4);
    gtk_list_box_insert(GTK_LIST_BOX(st->listbox), row_label, -1);

    /* Auto-select it */
    GtkListBoxRow *first = gtk_list_box_get_row_at_index(
        GTK_LIST_BOX(st->listbox), 0);
    if (first)
        gtk_list_box_select_row(GTK_LIST_BOX(st->listbox), first);
    st->selected_idx = 0;

    gchar *status_text = g_strdup_printf("Detected: %s (%.4f°, %.4f°)",
        name, lat, lon);
    gtk_label_set_text(GTK_LABEL(st->status_label), status_text);
    g_free(status_text);

    gtk_widget_show_all(st->listbox);
    g_object_unref(jp);
    g_object_unref(msg);
}

/* ── Signal callbacks ────────────────────────────────────────────────── */
static void on_search_clicked(GtkButton *btn, gpointer ud) {
    (void)btn;
    SearchState *st = ud;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(st->entry));
    do_nominatim_search(st, text);
}

static void on_entry_activate(GtkEntry *entry, gpointer ud) {
    (void)entry;
    SearchState *st = ud;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(st->entry));
    do_nominatim_search(st, text);
}

static void on_auto_clicked(GtkButton *btn, gpointer ud) {
    (void)btn;
    do_ip_geolocation(ud);
}

static void on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer ud) {
    (void)box;
    SearchState *st = ud;
    if (row)
        st->selected_idx = gtk_list_box_row_get_index(row);
    else
        st->selected_idx = -1;
}

/* ── Public API ──────────────────────────────────────────────────────── */
void weather_search_dialog(WeatherConfig *cfg, GtkWindow *parent) {
    SearchState *st = g_new0(SearchState, 1);
    st->cfg = cfg;
    st->selected_idx = -1;

    /* Create dialog */
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "Search Location",
        parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK",     GTK_RESPONSE_OK,
        NULL);
    st->dialog = dlg;
    gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 380);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(vbox, 16);
    gtk_widget_set_margin_end(vbox,   16);
    gtk_widget_set_margin_top(vbox,   12);
    gtk_widget_set_margin_bottom(vbox, 8);
    gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);

    /* ── Search row: [entry] [Search] [📍 Auto] ────────────────── */
    GtkWidget *search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), search_row, FALSE, FALSE, 0);

    st->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(st->entry),
        "Type a city name (e.g. London, Paris, Tokyo)…");
    gtk_widget_set_hexpand(st->entry, TRUE);
    gtk_box_pack_start(GTK_BOX(search_row), st->entry, TRUE, TRUE, 0);

    GtkWidget *btn_search = gtk_button_new_with_label("Search");
    gtk_box_pack_start(GTK_BOX(search_row), btn_search, FALSE, FALSE, 0);

    GtkWidget *btn_auto = gtk_button_new_with_label("📍 Auto");
    gtk_widget_set_tooltip_text(btn_auto,
        "Detect location automatically via IP geolocation");
    gtk_box_pack_start(GTK_BOX(search_row), btn_auto, FALSE, FALSE, 0);

    /* ── Results list ──────────────────────────────────────────── */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll),
        GTK_SHADOW_IN);
    gtk_widget_set_size_request(scroll, -1, 160);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    st->listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(st->listbox),
        GTK_SELECTION_SINGLE);
    gtk_container_add(GTK_CONTAINER(scroll), st->listbox);

    /* ── Current location label ────────────────────────────────── */
    gchar *cur_text = g_strdup_printf("Current: %s  (%.4f°, %.4f°)",
        cfg->location ? cfg->location : "Not set",
        cfg->lat, cfg->lon);
    GtkWidget *lbl_current = gtk_label_new(cur_text);
    g_free(cur_text);
    gtk_label_set_xalign(GTK_LABEL(lbl_current), 0.0f);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs,
        pango_attr_foreground_alpha_new(32768)); /* 50% opacity */
    gtk_label_set_attributes(GTK_LABEL(lbl_current), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_current, FALSE, FALSE, 0);

    /* ── Status label ──────────────────────────────────────────── */
    st->status_label = gtk_label_new("Type a city name and click Search");
    gtk_label_set_xalign(GTK_LABEL(st->status_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(vbox), st->status_label, FALSE, FALSE, 0);

    /* ── Signals ───────────────────────────────────────────────── */
    g_signal_connect(btn_search, "clicked",
        G_CALLBACK(on_search_clicked), st);
    g_signal_connect(st->entry, "activate",
        G_CALLBACK(on_entry_activate), st);
    g_signal_connect(btn_auto, "clicked",
        G_CALLBACK(on_auto_clicked), st);
    g_signal_connect(st->listbox, "row-selected",
        G_CALLBACK(on_row_selected), st);

    /* ── Run dialog ────────────────────────────────────────────── */
    gtk_widget_show_all(dlg);
    gtk_widget_grab_focus(st->entry);

    gint response = gtk_dialog_run(GTK_DIALOG(dlg));

    if (response == GTK_RESPONSE_OK && st->selected_idx >= 0) {
        GList *nth = g_list_nth(st->results, st->selected_idx);
        if (nth) {
            SearchResult *r = nth->data;
            g_free(cfg->location);
            cfg->location = g_strdup(r->display_name);
            cfg->lat = r->lat;
            cfg->lon = r->lon;
            weather_config_save(cfg);
        }
    }

    /* Cleanup */
    clear_results(st);
    gtk_widget_destroy(dlg);
    g_free(st);
}
