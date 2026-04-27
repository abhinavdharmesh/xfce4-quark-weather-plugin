#include "weather-ui.h"
#include "weather-icons.h"
#include <gdk/gdkx.h>
#include <math.h>
#include <time.h>

/* ═══════════════════════════════════════════════════════════════════════
 * Embedded detail-card SVG icons  (24×24 viewBox, white stroke, no fill)
 * ═══════════════════════════════════════════════════════════════════════ */
#define SVG_PRE "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'" \
                " fill='none' stroke='white' stroke-width='1.5'" \
                " stroke-linecap='round' stroke-linejoin='round'>"
#define SVG_SUF "</svg>"

static const gchar *SVG_HUMIDITY =
    SVG_PRE "<path d='M12 2L5.5 14a7 7 0 1 0 13 0Z'/>" SVG_SUF;

static const gchar *SVG_WIND =
    SVG_PRE "<path d='M3 8h10a3 3 0 1 0-3-3'/>"
            "<path d='M3 12h14a4 4 0 1 1-4 4'/>"
            "<path d='M3 16h6'/>" SVG_SUF;

static const gchar *SVG_PRESSURE =
    SVG_PRE "<circle cx='12' cy='12' r='9'/>"
            "<path d='M12 7v5l3 3'/>"
            "<circle cx='12' cy='12' r='1' fill='white'/>" SVG_SUF;

static const gchar *SVG_UV =
    SVG_PRE "<circle cx='12' cy='12' r='4'/>"
            "<line x1='12' y1='2' x2='12' y2='5'/>"
            "<line x1='12' y1='19' x2='12' y2='22'/>"
            "<line x1='2' y1='12' x2='5' y2='12'/>"
            "<line x1='19' y1='12' x2='22' y2='12'/>"
            "<line x1='4.93' y1='4.93' x2='7.07' y2='7.07'/>"
            "<line x1='16.93' y1='16.93' x2='19.07' y2='19.07'/>"
            "<line x1='4.93' y1='19.07' x2='7.07' y2='16.93'/>"
            "<line x1='16.93' y1='7.07' x2='19.07' y2='4.93'/>" SVG_SUF;

static const gchar *SVG_THERMOMETER =
    SVG_PRE "<path d='M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z'/>"
            "<line x1='9' y1='9' x2='9.01' y2='9'/>" SVG_SUF;

static const gchar *SVG_EYE =
    SVG_PRE "<path d='M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z'/>"
            "<circle cx='12' cy='12' r='3'/>" SVG_SUF;


/* ─── helpers ─────────────────────────────────────────────────────────── */
static GtkWidget *make_image_from_svg_data(const gchar *svg, gint size) {
    GdkPixbuf *pb = weather_icon_load_from_data(svg, size);
    if (!pb) return gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_MENU);
    GtkWidget *img = gtk_image_new_from_pixbuf(pb);
    g_object_unref(pb);
    return img;
}

static GtkWidget *make_section_label(const gchar *text) {
    GtkWidget *lbl = gtk_label_new(text);
    gtk_widget_set_name(lbl, "section-label");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_margin_top(lbl, 4);
    return lbl;
}

/* ─── UV label + colour coding ────────────────────────────────────────── */
static const gchar *uv_label(gdouble uv) {
    if (uv < 3)  return "Low";
    if (uv < 6)  return "Moderate";
    if (uv < 8)  return "High";
    if (uv < 11) return "Very High";
    return "Extreme";
}
static const gchar *uv_color(gdouble uv) {
    if (uv < 3)  return "#57e389";   /* green  */
    if (uv < 6)  return "#f6d32d";   /* yellow */
    if (uv < 8)  return "#ff7800";   /* orange */
    if (uv < 11) return "#e01b24";   /* red    */
    return "#813d9c";                /* purple */
}

/* ─── wind direction degrees → compass label ─────────────────────────── */
static const gchar *deg_to_compass(gdouble d) {
    static const gchar *dirs[] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    guint idx = (guint)((d + 11.25) / 22.5) % 16;
    return dirs[idx];
}

/* ─── visibility from fog_fraction ───────────────────────────────────── */
static const gchar *visibility_label(gdouble fog) {
    if (fog < 10.0) return "Clear";
    if (fog < 30.0) return "Hazy";
    return "Foggy";
}

/* ─── time formatting ─────────────────────────────────────────────────── */
static gchar *fmt_time(gint64 unix_ts) {
    if (!unix_ts) return g_strdup("--:--");
    GDateTime *dt  = g_date_time_new_from_unix_local(unix_ts);
    gchar     *str = g_date_time_format(dt, "%H:%M");
    g_date_time_unref(dt);
    return str;
}

static gchar *fmt_hour(gint64 unix_ts) {
    if (!unix_ts) return g_strdup("--");
    GDateTime *dt  = g_date_time_new_from_unix_local(unix_ts);
    gchar     *str = g_date_time_format(dt, "%H:00");
    g_date_time_unref(dt);
    return str;
}

static gchar *fmt_day_abbrev(gint64 unix_ts) {
    if (!unix_ts) return g_strdup("???");
    GDateTime *dt  = g_date_time_new_from_unix_local(unix_ts);
    gchar     *str = g_date_time_format(dt, "%a");
    g_date_time_unref(dt);
    return str;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Window setup helpers
 * ═══════════════════════════════════════════════════════════════════════ */
static void set_rgba_visual(GtkWidget *win) {
    GdkScreen *screen = gtk_widget_get_screen(win);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(win, visual);
    gtk_widget_set_app_paintable(win, TRUE);
}

static gboolean on_draw_bg(GtkWidget *w, cairo_t *cr, gpointer ud) {
    (void)ud;
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    gtk_render_background(sc, cr, 0, 0,
        gtk_widget_get_allocated_width(w),
        gtk_widget_get_allocated_height(w));
    return FALSE;
}

static gboolean on_focus_out(GtkWidget *w, GdkEventFocus *ev, gpointer ud) {
    (void)ev; (void)ud;
    gtk_widget_hide(w);
    return TRUE;
}

/* ─── global CSS ──────────────────────────────────────────────────────── */
static const gchar *POPUP_CSS =
    /* Window */
    "window {"
    "  background: rgba(12,12,18,0.68);"
    "  border-radius: 20px;"
    "  border: 1px solid rgba(255,255,255,0.08);"
    "}"
    /* Section labels */
    "#section-label {"
    "  color: rgba(255,255,255,0.40);"
    "  font-size: 10px;"
    "  font-weight: 700;"
    "  letter-spacing: 1.2px;"
    "}"
    /* Location / datetime */
    "#lbl-loc {"
    "  color: rgba(255,255,255,0.65);"
    "  font-size: 13px;"
    "}"
    "#lbl-datetime {"
    "  color: rgba(255,255,255,0.40);"
    "  font-size: 12px;"
    "}"
    /* Hero temperature */
    "#big-temp {"
    "  color: #ffffff;"
    "  font-size: 52px;"
    "  font-weight: 200;"
    "}"
    "#lbl-condition {"
    "  color: rgba(255,255,255,0.75);"
    "  font-size: 15px;"
    "}"
    "#lbl-feels {"
    "  color: rgba(255,255,255,0.45);"
    "  font-size: 12px;"
    "}"
    /* Detail cards */
    ".detail-card {"
    "  background: rgba(255,255,255,0.06);"
    "  border-radius: 12px;"
    "  border: 1px solid rgba(255,255,255,0.07);"
    "}"
    ".card-label {"
    "  color: rgba(255,255,255,0.42);"
    "  font-size: 11px;"
    "}"
    ".card-value {"
    "  color: rgba(255,255,255,0.90);"
    "  font-size: 13px;"
    "  font-weight: 600;"
    "}"
    /* Hourly cells */
    ".hourly-cell {"
    "  background: rgba(255,255,255,0.04);"
    "  border-radius: 10px;"
    "}"
    ".hourly-hour {"
    "  color: rgba(255,255,255,0.45);"
    "  font-size: 11px;"
    "}"
    ".hourly-temp {"
    "  color: rgba(255,255,255,0.85);"
    "  font-size: 13px;"
    "  font-weight: 500;"
    "}"
    /* Daily rows */
    ".daily-day {"
    "  color: rgba(255,255,255,0.75);"
    "  font-size: 13px;"
    "  min-width: 36px;"
    "}"
    ".daily-temps {"
    "  color: rgba(255,255,255,0.75);"
    "  font-size: 12px;"
    "}"
    /* Generic label fallback */
    "label { color: rgba(255,255,255,0.85); }";

/* (Sunrise arc removed — replaced by text row) */

/* ═══════════════════════════════════════════════════════════════════════
 * Detail card builder
 * ═══════════════════════════════════════════════════════════════════════ */
static GtkWidget *make_detail_card(const gchar *svg_icon,
                                    const gchar *label_text,
                                    const gchar *value_text,
                                    const gchar *widget_name_val) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_get_style_context(card);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "detail-card");
    gtk_widget_set_margin_start(card, 2);
    gtk_widget_set_margin_end(card, 2);
    gtk_widget_set_margin_top(card, 2);
    gtk_widget_set_margin_bottom(card, 2);

    /* inner padding via a nested box */
    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(inner,  10);
    gtk_widget_set_margin_end(inner,    10);
    gtk_widget_set_margin_top(inner,    10);
    gtk_widget_set_margin_bottom(inner, 10);
    gtk_box_pack_start(GTK_BOX(card), inner, TRUE, TRUE, 0);

    /* icon */
    GtkWidget *img = make_image_from_svg_data(svg_icon, 18);
    gtk_box_pack_start(GTK_BOX(inner), img, FALSE, FALSE, 0);

    /* label + value stacked vertically */
    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(inner), text_box, TRUE, TRUE, 0);

    GtkWidget *lbl = gtk_label_new(label_text);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "card-label");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_box_pack_start(GTK_BOX(text_box), lbl, FALSE, FALSE, 0);

    GtkWidget *val = gtk_label_new(value_text ? value_text : "--");
    gtk_style_context_add_class(gtk_widget_get_style_context(val), "card-value");
    gtk_label_set_xalign(GTK_LABEL(val), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(val), PANGO_ELLIPSIZE_END);
    if (widget_name_val) gtk_widget_set_name(val, widget_name_val);
    gtk_box_pack_start(GTK_BOX(text_box), val, FALSE, FALSE, 0);

    return card;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Create popup skeleton
 * ═══════════════════════════════════════════════════════════════════════ */
GtkWidget *weather_ui_create_popup(XfcePanelPlugin *plugin) {
    (void)plugin;

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    set_rgba_visual(win);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(win), 380, 640);
    gtk_widget_set_size_request(win, 380, -1);

    g_signal_connect(win, "draw", G_CALLBACK(on_draw_bg), NULL);
    g_signal_connect(win, "focus-out-event", G_CALLBACK(on_focus_out), NULL);

    /* Apply CSS */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, POPUP_CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gtk_widget_get_screen(win),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* ── Scrolled window so content doesn't clip on small panels ─── */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll),
        GTK_SHADOW_NONE);
    gtk_container_add(GTK_CONTAINER(win), scroll);

    /* ── Main vertical box ─────────────────────────────────────── */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox,  18);
    gtk_widget_set_margin_end(vbox,    18);
    gtk_widget_set_margin_top(vbox,    18);
    gtk_widget_set_margin_bottom(vbox, 18);
    gtk_container_add(GTK_CONTAINER(scroll), vbox);

    /* ── A. Header: location (left) + date (right) ─────────────── */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    GtkWidget *lbl_loc = gtk_label_new("Loading…");
    gtk_widget_set_name(lbl_loc, "lbl-loc");
    gtk_label_set_xalign(GTK_LABEL(lbl_loc), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(lbl_loc), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(header), lbl_loc, TRUE, TRUE, 0);

    GtkWidget *lbl_datetime = gtk_label_new("");
    gtk_widget_set_name(lbl_datetime, "lbl-datetime");
    gtk_label_set_xalign(GTK_LABEL(lbl_datetime), 1.0f);
    gtk_box_pack_end(GTK_BOX(header), lbl_datetime, FALSE, FALSE, 0);

    /* ── B. Hero: icon + temp + condition + feels-like ─────────── */
    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_pack_start(GTK_BOX(vbox), hero, FALSE, FALSE, 0);

    /* Left: large weather icon */
    GtkWidget *img_hero = gtk_image_new_from_icon_name(
        "weather-few-clouds-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_widget_set_name(img_hero, "img-hero");
    gtk_widget_set_valign(img_hero, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(hero), img_hero, FALSE, FALSE, 0);

    /* Right: text stacked */
    GtkWidget *hero_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(hero), hero_text, TRUE, TRUE, 0);

    GtkWidget *lbl_temp = gtk_label_new("--°");
    gtk_widget_set_name(lbl_temp, "big-temp");
    gtk_label_set_xalign(GTK_LABEL(lbl_temp), 0.0f);
    gtk_box_pack_start(GTK_BOX(hero_text), lbl_temp, FALSE, FALSE, 0);

    GtkWidget *lbl_cond = gtk_label_new("");
    gtk_widget_set_name(lbl_cond, "lbl-condition");
    gtk_label_set_xalign(GTK_LABEL(lbl_cond), 0.0f);
    gtk_box_pack_start(GTK_BOX(hero_text), lbl_cond, FALSE, FALSE, 0);

    GtkWidget *lbl_feels = gtk_label_new("");
    gtk_widget_set_name(lbl_feels, "lbl-feels");
    gtk_label_set_xalign(GTK_LABEL(lbl_feels), 0.0f);
    gtk_box_pack_start(GTK_BOX(hero_text), lbl_feels, FALSE, FALSE, 0);

    /* ── C. Detail cards 2×3 grid ──────────────────────────────── */
    gtk_box_pack_start(GTK_BOX(vbox), make_section_label("DETAILS"), FALSE, FALSE, 0);

    GtkWidget *card_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(card_grid),    4);
    gtk_grid_set_column_spacing(GTK_GRID(card_grid), 4);
    gtk_grid_set_column_homogeneous(GTK_GRID(card_grid), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), card_grid, FALSE, FALSE, 0);

    /* Card definitions: [row][col] */
    struct { const gchar *svg; const gchar *label; const gchar *name; } cards[3][2] = {
        { { SVG_HUMIDITY,    "Humidity",   "cv-humidity"  },
          { SVG_WIND,        "Wind",       "cv-wind"      } },
        { { SVG_PRESSURE,    "Pressure",   "cv-pressure"  },
          { SVG_UV,          "UV Index",   "cv-uv"        } },
        { { SVG_THERMOMETER, "Dew Point",  "cv-dew"       },
          { SVG_EYE,         "Visibility", "cv-vis"       } },
    };

    for (gint row = 0; row < 3; row++) {
        for (gint col = 0; col < 2; col++) {
            GtkWidget *card = make_detail_card(
                cards[row][col].svg,
                cards[row][col].label,
                "--",
                cards[row][col].name);
            gtk_grid_attach(GTK_GRID(card_grid), card, col, row, 1, 1);
            gtk_widget_set_hexpand(card, TRUE);
        }
    }

    /* ── D. Sunrise / Sunset (simple text row) ─────────────────── */
    gtk_box_pack_start(GTK_BOX(vbox), make_section_label("SUNRISE & SUNSET"), FALSE, FALSE, 0);

    GtkWidget *lbl_sun_row = gtk_label_new("☀  Sunrise —  ·  Sunset —");
    gtk_widget_set_name(lbl_sun_row, "lbl-sun-row");
    gtk_style_context_add_class(
        gtk_widget_get_style_context(lbl_sun_row), "card-label");
    gtk_widget_set_halign(lbl_sun_row, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(lbl_sun_row, 4);
    gtk_widget_set_margin_bottom(lbl_sun_row, 4);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_sun_row, FALSE, FALSE, 0);

    /* ── E. Hourly forecast ────────────────────────────────────── */
    gtk_box_pack_start(GTK_BOX(vbox), make_section_label("HOURLY FORECAST"), FALSE, FALSE, 0);

    GtkWidget *hscroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(hscroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(hscroll), GTK_SHADOW_NONE);
    gtk_widget_set_size_request(hscroll, -1, 86);
    gtk_box_pack_start(GTK_BOX(vbox), hscroll, FALSE, FALSE, 0);

    GtkWidget *hourly_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_name(hourly_box, "hourly-box");
    gtk_widget_set_margin_top(hourly_box, 2);
    gtk_widget_set_margin_bottom(hourly_box, 2);
    gtk_container_add(GTK_CONTAINER(hscroll), hourly_box);

    /* ── F. 7-Day forecast ─────────────────────────────────────── */
    gtk_box_pack_start(GTK_BOX(vbox), make_section_label("7-DAY FORECAST"), FALSE, FALSE, 0);

    GtkWidget *daily_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(daily_box, "daily-box");
    gtk_box_pack_start(GTK_BOX(vbox), daily_box, FALSE, FALSE, 0);

    /* ── Store widget references ───────────────────────────────── */
    g_object_set_data(G_OBJECT(win), "lbl-loc",      lbl_loc);
    g_object_set_data(G_OBJECT(win), "lbl-datetime", lbl_datetime);
    g_object_set_data(G_OBJECT(win), "img-hero",     img_hero);
    g_object_set_data(G_OBJECT(win), "lbl-temp",     lbl_temp);
    g_object_set_data(G_OBJECT(win), "lbl-cond",     lbl_cond);
    g_object_set_data(G_OBJECT(win), "lbl-feels",    lbl_feels);
    g_object_set_data(G_OBJECT(win), "card-grid",    card_grid);
    g_object_set_data(G_OBJECT(win), "lbl-sun-row",  lbl_sun_row);
    g_object_set_data(G_OBJECT(win), "hourly-box",   hourly_box);
    g_object_set_data(G_OBJECT(win), "daily-box",    daily_box);

    return win;
}

/* ═══════════════════════════════════════════════════════════════════════
 * find_named: walk container tree for a widget with a given name
 * ═══════════════════════════════════════════════════════════════════════ */
static GtkWidget *find_named(GtkWidget *root, const gchar *name) {
    if (!root) return NULL;
    if (g_strcmp0(gtk_widget_get_name(root), name) == 0) return root;
    if (GTK_IS_CONTAINER(root)) {
        GList *ch = gtk_container_get_children(GTK_CONTAINER(root));
        for (GList *l = ch; l; l = l->next) {
            GtkWidget *found = find_named(GTK_WIDGET(l->data), name);
            if (found) { g_list_free(ch); return found; }
        }
        g_list_free(ch);
    }
    return NULL;
}

/* Update a named label value inside the card grid */
static void update_card_value(GtkWidget *card_grid,
                               const gchar *name, const gchar *text) {
    GtkWidget *w = find_named(card_grid, name);
    if (w && GTK_IS_LABEL(w)) gtk_label_set_text(GTK_LABEL(w), text);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Daily temperature-range bar drawing callback (file-scope, not nested)
 * ═══════════════════════════════════════════════════════════════════════ */
static gboolean draw_temp_bar(GtkWidget *w, cairo_t *cr, gpointer ud) {
    (void)ud;
    gdouble *fl = g_object_get_data(G_OBJECT(w), "frac-lo");
    gdouble *fh = g_object_get_data(G_OBJECT(w), "frac-hi");
    if (!fl || !fh) return FALSE;

    gint bw = gtk_widget_get_allocated_width(w);
    gint bh = gtk_widget_get_allocated_height(w);

    /* track */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
    cairo_rectangle(cr, 0, 0, bw, bh);
    cairo_fill(cr);

    /* filled range */
    gdouble x0 = (*fl) * bw;
    gdouble x1 = (*fh) * bw;
    cairo_pattern_t *grad = cairo_pattern_create_linear(x0, 0, x1, 0);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, 0.4, 0.7, 1.0, 0.8);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, 1.0, 0.5, 0.2, 0.9);
    cairo_set_source(cr, grad);
    cairo_rectangle(cr, x0, 0, MAX(x1 - x0, 4), bh);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
    return FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Update popup with live data
 * ═══════════════════════════════════════════════════════════════════════ */
void weather_ui_update_popup(GtkWidget         *win,
                              const WeatherData *d,
                              const WeatherConfig *cfg) {
    if (!win || !d || !cfg) return;

    GtkWidget *lbl_loc      = g_object_get_data(G_OBJECT(win), "lbl-loc");
    GtkWidget *lbl_datetime = g_object_get_data(G_OBJECT(win), "lbl-datetime");
    GtkWidget *img_hero     = g_object_get_data(G_OBJECT(win), "img-hero");
    GtkWidget *lbl_temp     = g_object_get_data(G_OBJECT(win), "lbl-temp");
    GtkWidget *lbl_cond     = g_object_get_data(G_OBJECT(win), "lbl-cond");
    GtkWidget *lbl_feels    = g_object_get_data(G_OBJECT(win), "lbl-feels");
    GtkWidget *card_grid    = g_object_get_data(G_OBJECT(win), "card-grid");
    GtkWidget *lbl_sun_row  = g_object_get_data(G_OBJECT(win), "lbl-sun-row");
    GtkWidget *hourly_box   = g_object_get_data(G_OBJECT(win), "hourly-box");
    GtkWidget *daily_box    = g_object_get_data(G_OBJECT(win), "daily-box");

    const WeatherCurrent *c = &d->current;

    /* -- Header -------------------------------------------------------- */
    if (lbl_loc)
        gtk_label_set_text(GTK_LABEL(lbl_loc),
            cfg->location ? cfg->location : "Unknown");

    if (lbl_datetime) {
        GDateTime *now = g_date_time_new_now_local();
        gchar *dtstr = g_date_time_format(now, "%a %b %d  %H:%M");
        gtk_label_set_text(GTK_LABEL(lbl_datetime), dtstr);
        g_free(dtstr);
        g_date_time_unref(now);
    }

    /* -- Hero icon ----------------------------------------------------- */
    if (img_hero && c->symbol_code) {
        GdkPixbuf *pb = weather_icon_load(c->symbol_code, 64);
        if (pb) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(img_hero), pb);
            g_object_unref(pb);
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(img_hero),
                c->icon_name ? c->icon_name : "weather-few-clouds-symbolic",
                GTK_ICON_SIZE_DIALOG);
        }
    }

    /* -- Temperature & condition --------------------------------------- */
    if (lbl_temp) {
        gdouble t = cfg->use_fahrenheit ? c->temp_c * 9.0/5.0 + 32.0 : c->temp_c;
        gchar *ts = g_strdup_printf("%.0f°%s", t, cfg->use_fahrenheit ? "F" : "C");
        gtk_label_set_text(GTK_LABEL(lbl_temp), ts);
        g_free(ts);
    }
    if (lbl_cond && c->condition)
        gtk_label_set_text(GTK_LABEL(lbl_cond), c->condition);

    if (lbl_feels) {
        /* feels_like_c == 0.0 means not computed (no wind/extreme temp) — show actual temp */
        gdouble fl_raw = (c->feels_like_c != 0.0) ? c->feels_like_c : c->temp_c;
        gdouble fl = cfg->use_fahrenheit ? fl_raw * 9.0/5.0 + 32.0 : fl_raw;
        gdouble t  = cfg->use_fahrenheit ? c->temp_c * 9.0/5.0 + 32.0 : c->temp_c;
        gchar *fs;
        if (fabs(fl - t) < 1.0)
            fs = g_strdup_printf("Humidity %.0f%%  ·  Precip %.1f mm",
                                  c->humidity, c->precip_mm);
        else
            fs = g_strdup_printf("Feels like %.0f°%s",
                fl, cfg->use_fahrenheit ? "F" : "C");
        gtk_label_set_text(GTK_LABEL(lbl_feels), fs);
        g_free(fs);
    }

    /* -- Detail cards -------------------------------------------------- */
    if (card_grid) {
        gchar *s;

        s = g_strdup_printf("%.0f%%", c->humidity);
        update_card_value(card_grid, "cv-humidity", s); g_free(s);

        if (c->wind_gust_kph > 0)
            s = g_strdup_printf("%.0f km/h %s\nGusts: %.0f km/h",
                c->wind_kph, deg_to_compass(c->wind_deg), c->wind_gust_kph);
        else
            s = g_strdup_printf("%.0f km/h %s",
                c->wind_kph, deg_to_compass(c->wind_deg));
        update_card_value(card_grid, "cv-wind", s); g_free(s);

        s = c->pressure_hpa > 0
            ? g_strdup_printf("%.0f hPa", c->pressure_hpa)
            : g_strdup("—");
        update_card_value(card_grid, "cv-pressure", s); g_free(s);

        /* UV with inline color markup */
        gchar *uv_str = c->uv_index > 0
            ? g_strdup_printf(
                "<span color='%s'>%.1f</span>  <span color='%s' size='small'>%s</span>",
                uv_color(c->uv_index), c->uv_index,
                uv_color(c->uv_index), uv_label(c->uv_index))
            : g_strdup("<span color='#57e389'>0.0</span>  <span size='small'>Low</span>");
        GtkWidget *uv_w = find_named(card_grid, "cv-uv");
        if (uv_w && GTK_IS_LABEL(uv_w))
            gtk_label_set_markup(GTK_LABEL(uv_w), uv_str);
        g_free(uv_str);

        gdouble dew = cfg->use_fahrenheit
            ? c->dew_point_c * 9.0/5.0 + 32.0 : c->dew_point_c;
        s = c->dew_point_c != 0.0
            ? g_strdup_printf("%.0f°%s", dew, cfg->use_fahrenheit ? "F" : "C")
            : g_strdup("—");
        update_card_value(card_grid, "cv-dew", s); g_free(s);

        update_card_value(card_grid, "cv-vis", visibility_label(c->fog_fraction));
    }

    /* -- Sunrise / sunset text row ------------------------------------- */
    if (lbl_sun_row) {
        if (d->sunrise > 0 && d->sunset > 0) {
            gchar *rise = fmt_time(d->sunrise);
            gchar *set  = fmt_time(d->sunset);
            gint64 len_sec = d->sunset - d->sunrise;
            gchar *txt = g_strdup_printf(
                "☀  Sunrise %s  ·  Sunset %s  ·  Daylight %dh %02dm",
                rise, set,
                (gint)(len_sec / 3600),
                (gint)((len_sec % 3600) / 60));
            gtk_label_set_text(GTK_LABEL(lbl_sun_row), txt);
            g_free(rise); g_free(set); g_free(txt);
        } else {
            gtk_label_set_text(GTK_LABEL(lbl_sun_row),
                "☀  Sunrise —  ·  Sunset —");
        }
    }

    /* -- Hourly (rebuild) ---------------------------------------------- */
    if (hourly_box) {
        GList *ch = gtk_container_get_children(GTK_CONTAINER(hourly_box));
        for (GList *l = ch; l; l = l->next)
            gtk_widget_destroy(GTK_WIDGET(l->data));
        g_list_free(ch);

        gint count = MIN(d->hourly_count, 12);
        for (gint i = 0; i < count; i++) {
            const WeatherHour *h = &d->hourly[i];

            GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
            gtk_style_context_add_class(gtk_widget_get_style_context(cell),
                                        "hourly-cell");
            gtk_widget_set_margin_start(cell, 2);
            gtk_widget_set_margin_end(cell, 2);
            gtk_widget_set_margin_top(cell, 4);
            gtk_widget_set_margin_bottom(cell, 4);

            /* inner padding */
            GtkWidget *inn = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
            gtk_widget_set_margin_start(inn,  7);
            gtk_widget_set_margin_end(inn,    7);
            gtk_widget_set_margin_top(inn,    6);
            gtk_widget_set_margin_bottom(inn, 6);
            gtk_box_pack_start(GTK_BOX(cell), inn, TRUE, TRUE, 0);

            /* Hour label */
            gchar *hr_str = fmt_hour(h->time);
            GtkWidget *lbl_hr = gtk_label_new(hr_str);
            g_free(hr_str);
            gtk_style_context_add_class(gtk_widget_get_style_context(lbl_hr),
                                        "hourly-hour");
            gtk_box_pack_start(GTK_BOX(inn), lbl_hr, FALSE, FALSE, 0);

            /* Weather icon */
            GdkPixbuf *pb = h->symbol_code
                ? weather_icon_load(h->symbol_code, 24) : NULL;
            GtkWidget *img = pb
                ? gtk_image_new_from_pixbuf(pb)
                : gtk_image_new_from_icon_name("weather-overcast-symbolic",
                                               GTK_ICON_SIZE_SMALL_TOOLBAR);
            if (pb) g_object_unref(pb);
            gtk_box_pack_start(GTK_BOX(inn), img, FALSE, FALSE, 0);

            /* Temperature */
            gdouble ht = cfg->use_fahrenheit
                ? h->temp_c * 9.0/5.0 + 32.0 : h->temp_c;
            gchar *t_str = g_strdup_printf("%.0f°", ht);
            GtkWidget *lbl_ht = gtk_label_new(t_str);
            g_free(t_str);
            gtk_style_context_add_class(gtk_widget_get_style_context(lbl_ht),
                                        "hourly-temp");
            gtk_box_pack_start(GTK_BOX(inn), lbl_ht, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(hourly_box), cell, FALSE, FALSE, 0);
        }
        gtk_widget_show_all(hourly_box);
    }

    /* -- Daily (rebuild) ----------------------------------------------- */
    if (daily_box) {
        GList *ch = gtk_container_get_children(GTK_CONTAINER(daily_box));
        for (GList *l = ch; l; l = l->next)
            gtk_widget_destroy(GTK_WIDGET(l->data));
        g_list_free(ch);

        /* Determine min/max across all days for the temp-bar scale */
        gdouble all_min = 9999, all_max = -9999;
        for (gint i = 0; i < d->daily_count; i++) {
            if (d->daily[i].temp_min_c < all_min) all_min = d->daily[i].temp_min_c;
            if (d->daily[i].temp_max_c > all_max) all_max = d->daily[i].temp_max_c;
        }
        gdouble range = MAX(all_max - all_min, 1.0);

        for (gint i = 0; i < d->daily_count; i++) {
            const WeatherDay *day = &d->daily[i];

            /* Separator (except first row) */
            if (i > 0) {
                GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
                GtkCssProvider *sep_css = gtk_css_provider_new();
                gtk_css_provider_load_from_data(sep_css,
                    "separator { background: rgba(255,255,255,0.07); min-height: 1px; }",
                    -1, NULL);
                gtk_style_context_add_provider(
                    gtk_widget_get_style_context(sep),
                    GTK_STYLE_PROVIDER(sep_css),
                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                g_object_unref(sep_css);
                gtk_box_pack_start(GTK_BOX(daily_box), sep, FALSE, FALSE, 0);
            }

            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_set_margin_top(row, 6);
            gtk_widget_set_margin_bottom(row, 6);
            gtk_box_pack_start(GTK_BOX(daily_box), row, FALSE, FALSE, 0);

            /* Day name */
            gchar *day_name = fmt_day_abbrev(day->date);
            GtkWidget *lbl_day = gtk_label_new(day_name);
            g_free(day_name);
            gtk_style_context_add_class(gtk_widget_get_style_context(lbl_day),
                                        "daily-day");
            gtk_widget_set_size_request(lbl_day, 38, -1);
            gtk_label_set_xalign(GTK_LABEL(lbl_day), 0.0f);
            gtk_box_pack_start(GTK_BOX(row), lbl_day, FALSE, FALSE, 0);

            /* Icon */
            GdkPixbuf *dpb = day->symbol_code
                ? weather_icon_load(day->symbol_code, 22) : NULL;
            GtkWidget *dimg = dpb
                ? gtk_image_new_from_pixbuf(dpb)
                : gtk_image_new_from_icon_name("weather-overcast-symbolic",
                                               GTK_ICON_SIZE_SMALL_TOOLBAR);
            if (dpb) g_object_unref(dpb);
            gtk_box_pack_start(GTK_BOX(row), dimg, FALSE, FALSE, 0);

            /* Temperature bar */
            gdouble lo = cfg->use_fahrenheit
                ? day->temp_min_c * 9.0/5.0 + 32.0 : day->temp_min_c;
            gdouble hi = cfg->use_fahrenheit
                ? day->temp_max_c * 9.0/5.0 + 32.0 : day->temp_max_c;

            /* low temp label */
            gchar *lo_str = g_strdup_printf("%.0f°", lo);
            GtkWidget *lbl_lo = gtk_label_new(lo_str);
            g_free(lo_str);
            gtk_style_context_add_class(gtk_widget_get_style_context(lbl_lo),
                                        "daily-temps");
            gtk_label_set_xalign(GTK_LABEL(lbl_lo), 1.0f);
            gtk_widget_set_size_request(lbl_lo, 34, -1);
            gtk_box_pack_start(GTK_BOX(row), lbl_lo, FALSE, FALSE, 0);

            /* gradient bar showing relative position of range */
            GtkWidget *bar_frame = gtk_frame_new(NULL);
            gtk_frame_set_shadow_type(GTK_FRAME(bar_frame), GTK_SHADOW_NONE);
            gtk_widget_set_size_request(bar_frame, -1, 6);
            gtk_widget_set_valign(bar_frame, GTK_ALIGN_CENTER);

            /* Represent as a CSS-coloured box — using a drawing area for precision */
            GtkWidget *bar = gtk_drawing_area_new();
            gtk_widget_set_size_request(bar, 80, 6);
            gdouble frac_lo = (day->temp_min_c - all_min) / range;
            gdouble frac_hi = (day->temp_max_c - all_min) / range;
            g_object_set_data(G_OBJECT(bar), "frac-lo",
                g_memdup2(&frac_lo, sizeof(gdouble)));
            g_object_set_data_full(G_OBJECT(bar), "frac-hi",
                g_memdup2(&frac_hi, sizeof(gdouble)), g_free);
            g_signal_connect(bar, "draw",
                G_CALLBACK(draw_temp_bar), NULL);
            gtk_container_add(GTK_CONTAINER(bar_frame), bar);
            gtk_box_pack_start(GTK_BOX(row), bar_frame, TRUE, TRUE, 0);

            /* high temp */
            gchar *hi_str = g_strdup_printf("%.0f°", hi);
            GtkWidget *lbl_hi = gtk_label_new(hi_str);
            g_free(hi_str);
            gtk_style_context_add_class(gtk_widget_get_style_context(lbl_hi),
                                        "daily-temps");
            gtk_label_set_xalign(GTK_LABEL(lbl_hi), 0.0f);
            gtk_widget_set_size_request(lbl_hi, 34, -1);
            gtk_box_pack_end(GTK_BOX(row), lbl_hi, FALSE, FALSE, 0);
        }
        gtk_widget_show_all(daily_box);
    }

    gtk_widget_show_all(win);
}

/* ── Error display ─────────────────────────────────────────────────────── */
void weather_ui_show_error(GtkWidget *win, const gchar *msg) {
    if (!win) return;
    GtkWidget *lbl = g_object_get_data(G_OBJECT(win), "lbl-temp");
    if (lbl) gtk_label_set_text(GTK_LABEL(lbl), msg ? msg : "Error");
    GtkWidget *cond = g_object_get_data(G_OBJECT(win), "lbl-cond");
    if (cond) gtk_label_set_text(GTK_LABEL(cond), "");
}

/* ── Positioning ───────────────────────────────────────────────────────── */
void weather_ui_position_popup(GtkWidget *win, XfcePanelPlugin *plugin) {
    if (!win || !plugin) return;
    gint x = 0, y = 0;
    xfce_panel_plugin_position_widget(plugin, win, NULL, &x, &y);
    gtk_window_move(GTK_WINDOW(win), x, y);
}

