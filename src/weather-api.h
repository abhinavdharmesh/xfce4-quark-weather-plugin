#pragma once
#include "weather-data.h"

typedef void (*WeatherCallback)(WeatherData *data, GError *err, gpointer user_data);

/* Async forecast fetch. Calls cb on the main thread when done. */
void weather_api_fetch(const WeatherConfig *cfg,
                       WeatherCallback      cb,
                       gpointer             user_data);

/* Async sunrise/sunset fetch. Patches data_ptr->sunrise/sunset in-place
 * then calls redraw_cb(plugin_ud) so the UI can refresh the arc widget.
 * Safe to call right after weather_api_fetch returns. */
void weather_api_fetch_sunrise(gdouble      lat,
                                gdouble      lon,
                                WeatherData **data_ptr,
                                void        (*redraw_cb)(gpointer),
                                gpointer     plugin_ud);

/* Cancel all in-flight requests — call from plugin destructor. */
void weather_api_cancel_all(void);
