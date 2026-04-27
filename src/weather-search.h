#pragma once
#include "weather-data.h"
#include <gtk/gtk.h>

/**
 * weather_search_dialog:
 * @cfg:    WeatherConfig to update (lat, lon, location) on success
 * @parent: parent window for the modal dialog (may be NULL)
 *
 * Opens a modal dialog allowing the user to search for a city name
 * via Nominatim (OpenStreetMap) or detect location via IP geolocation.
 * On OK, cfg->lat, cfg->lon, cfg->location are updated and saved.
 */
void weather_search_dialog(WeatherConfig *cfg, GtkWindow *parent);
