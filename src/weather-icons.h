#pragma once
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>
#include <glib.h>

/*
 * weather-icons.h — SVG weather icon loader
 *
 * Loads met.no weather SVGs from the installed icon directory, renders them
 * to GdkPixbuf at the requested size, and caches results in a GHashTable to
 * avoid redundant disk I/O.  Also provides a helper for rendering inline SVG
 * strings (used by the UI for the detail-card line-art icons).
 *
 * Requires: librsvg2-dev (provides the GdkPixbuf SVG loader at runtime)
 *           Install: sudo apt install librsvg2-dev
 */

/**
 * weather_icon_load:
 * @symbol_code: met.no symbol code, e.g. "clearsky_day", "partlycloudy_night"
 * @size:        square pixel size to render at
 *
 * Returns a GdkPixbuf (caller owns the reference) for the given symbol code.
 * Looks up the SVG in the installed icon directory, falls back to stripping
 * the day/night suffix, then falls back to a GTK symbolic theme icon.
 * Returns %NULL only if every fallback also fails.
 */
GdkPixbuf   *weather_icon_load           (const gchar *symbol_code, gint size);

/**
 * weather_icon_load_from_data:
 * @svg_data: NUL-terminated SVG XML string
 * @size:     square pixel size to render at
 *
 * Renders an in-memory SVG string to a GdkPixbuf.  Used for the detail-card
 * line-art icons that are embedded directly in weather-ui.c.
 * Caller owns the returned reference.
 */
GdkPixbuf   *weather_icon_load_from_data (const gchar *svg_data, gint size);

/**
 * weather_icon_dir:
 *
 * Returns the resolved path to the installed weather SVG icon directory.
 * The string is owned by the icon module; do not free.
 */
const gchar *weather_icon_dir            (void);

/**
 * weather_icons_cleanup:
 *
 * Releases the icon cache and frees all cached pixbufs.
 * Call once during plugin teardown.
 */
void         weather_icons_cleanup       (void);
