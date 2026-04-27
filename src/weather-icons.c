#include "weather-icons.h"
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

/* ── icon cache ────────────────────────────────────────────────────────── */
/* keyed by "symbol_code:size" or "data:<hash>:<size>", value is GdkPixbuf* */
static GHashTable *g_icon_cache = NULL;

static void ensure_cache(void) {
    if (!g_icon_cache)
        g_icon_cache = g_hash_table_new_full(g_str_hash, g_str_equal,
                                              g_free,
                                              (GDestroyNotify)g_object_unref);
}

/* ── icon directory resolution ─────────────────────────────────────────── */
static gchar *g_icon_dir_cache = NULL;

const gchar *weather_icon_dir(void) {
    if (g_icon_dir_cache)
        return g_icon_dir_cache;

    /* Ordered list of candidate paths */
    const gchar *candidates[] = {
        "/usr/share/xfce4/weather-plugin/icons/weather",
        "/usr/local/share/xfce4/weather-plugin/icons/weather",
        /* Development tree: icons/weather/ relative to the binary's prefix */
        NULL
    };

    /* Also check $XDG_DATA_HOME */
    const gchar *xdg = g_get_user_data_dir();
    gchar *xdg_path = g_build_filename(xdg, "xfce4", "weather-plugin",
                                        "icons", "weather", NULL);

    for (gint i = 0; candidates[i]; i++) {
        if (g_file_test(candidates[i], G_FILE_TEST_IS_DIR)) {
            g_icon_dir_cache = g_strdup(candidates[i]);
            g_free(xdg_path);
            return g_icon_dir_cache;
        }
    }
    if (g_file_test(xdg_path, G_FILE_TEST_IS_DIR)) {
        g_icon_dir_cache = xdg_path;
        return g_icon_dir_cache;
    }
    g_free(xdg_path);

    /* Final fallback — may not exist but gives a predictable path */
    g_icon_dir_cache = g_strdup("/usr/share/xfce4/weather-plugin/icons/weather");
    return g_icon_dir_cache;
}

/* ── helper: render SVG file to pixbuf via the GdkPixbuf SVG loader ──── */
static GdkPixbuf *load_svg_file(const gchar *path, gint size) {
    GError    *err    = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(path, size, size,
                                                           TRUE, &err);
    if (!pixbuf) {
        g_debug("weather-icons: could not load '%s': %s",
                path, err ? err->message : "unknown");
        g_clear_error(&err);
    }
    return pixbuf;
}

/* ── public API: load by symbol_code ───────────────────────────────────── */
GdkPixbuf *weather_icon_load(const gchar *symbol_code, gint size) {
    ensure_cache();

    if (!symbol_code || *symbol_code == '\0')
        symbol_code = "cloudy";

    /* Check cache */
    gchar *key = g_strdup_printf("%s:%d", symbol_code, size);
    GdkPixbuf *cached = g_hash_table_lookup(g_icon_cache, key);
    if (cached) {
        g_free(key);
        return g_object_ref(cached);
    }

    GdkPixbuf *pixbuf = NULL;

    /* 1. Try exact symbol_code */
    {
        gchar *path = g_strdup_printf("%s/%s.svg", weather_icon_dir(), symbol_code);
        pixbuf = load_svg_file(path, size);
        g_free(path);
    }

    /* 2. Strip day/night/polartwilight suffix and retry */
    if (!pixbuf) {
        gchar *base = g_strdup(symbol_code);
        gchar *under = strrchr(base, '_');
        if (under &&
            (g_strcmp0(under, "_day")           == 0 ||
             g_strcmp0(under, "_night")         == 0 ||
             g_strcmp0(under, "_polartwilight") == 0)) {
            *under = '\0';
            gchar *path = g_strdup_printf("%s/%s.svg", weather_icon_dir(), base);
            pixbuf = load_svg_file(path, size);
            g_free(path);
        }
        g_free(base);
    }

    /* 3. GTK symbolic icon fallback */
    if (!pixbuf) {
        GtkIconTheme *theme = gtk_icon_theme_get_default();
        /* Build a reasonable GTK icon name from the symbol */
        static const struct { const gchar *prefix; const gchar *gtk; } map[] = {
            { "clearsky",       "weather-clear"              },
            { "fair",           "weather-few-clouds"         },
            { "partlycloudy",   "weather-few-clouds"         },
            { "cloudy",         "weather-overcast"           },
            { "fog",            "weather-fog"                },
            { "rain",           "weather-showers"            },
            { "lightrain",      "weather-showers-scattered"  },
            { "heavyrain",      "weather-showers"            },
            { "snow",           "weather-snow"               },
            { "sleet",          "weather-sleet"              },
            { "thunder",        "weather-storm"              },
            { NULL,             NULL                         },
        };
        const gchar *gtk_name = "weather-few-clouds-symbolic";
        for (gint i = 0; map[i].prefix; i++) {
            if (g_str_has_prefix(symbol_code, map[i].prefix)) {
                gchar *try = g_strdup_printf("%s-symbolic", map[i].gtk);
                if (gtk_icon_theme_has_icon(theme, try)) {
                    g_free(try);
                    gtk_name = map[i].gtk;
                    break;
                }
                g_free(try);
            }
        }
        pixbuf = gtk_icon_theme_load_icon(theme, gtk_name, size, 0, NULL);
    }

    /* Cache and return */
    if (pixbuf)
        g_hash_table_insert(g_icon_cache, g_strdup(key), g_object_ref(pixbuf));

    g_free(key);
    return pixbuf; /* caller owns the ref */
}

/* ── public API: render inline SVG string ──────────────────────────────── */
GdkPixbuf *weather_icon_load_from_data(const gchar *svg_data, gint size) {
    if (!svg_data || !*svg_data) return NULL;
    ensure_cache();

    /* Cache key: hash of the svg_data string + size */
    gchar *key = g_strdup_printf("data:%u:%d", g_str_hash(svg_data), size);
    GdkPixbuf *cached = g_hash_table_lookup(g_icon_cache, key);
    if (cached) {
        g_free(key);
        return g_object_ref(cached);
    }

    GError           *err    = NULL;
    GdkPixbufLoader  *loader = gdk_pixbuf_loader_new_with_type("svg", &err);
    if (!loader) {
        g_debug("weather-icons: no SVG pixbuf loader — %s",
                err ? err->message : "unknown");
        g_clear_error(&err);
        g_free(key);
        return NULL;
    }

    /* Set desired output size before writing data */
    gdk_pixbuf_loader_set_size(loader, size, size);

    gdk_pixbuf_loader_write(loader, (const guchar *)svg_data,
                            (gsize)strlen(svg_data), NULL);
    gdk_pixbuf_loader_close(loader, NULL);

    GdkPixbuf *raw = gdk_pixbuf_loader_get_pixbuf(loader);
    GdkPixbuf *pixbuf = NULL;
    if (raw) {
        /* Scale to exact requested size if the loader respected it already */
        if (gdk_pixbuf_get_width(raw)  != size ||
            gdk_pixbuf_get_height(raw) != size) {
            pixbuf = gdk_pixbuf_scale_simple(raw, size, size,
                                             GDK_INTERP_BILINEAR);
        } else {
            pixbuf = g_object_ref(raw);
        }
        g_hash_table_insert(g_icon_cache, g_strdup(key), g_object_ref(pixbuf));
    }

    g_object_unref(loader);
    g_free(key);
    return pixbuf; /* caller owns */
}

/* ── cleanup ────────────────────────────────────────────────────────────── */
void weather_icons_cleanup(void) {
    if (g_icon_cache) {
        g_hash_table_destroy(g_icon_cache);
        g_icon_cache = NULL;
    }
    g_free(g_icon_dir_cache);
    g_icon_dir_cache = NULL;
}
