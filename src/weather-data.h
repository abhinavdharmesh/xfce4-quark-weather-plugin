#pragma once
#include <glib.h>

typedef enum {
    PROVIDER_METNO,
    PROVIDER_OWM,
    PROVIDER_WTTRIN,
} WeatherProvider;

typedef struct {
    gdouble temp_c;
    gdouble feels_like_c;       /* apparent temperature (wind chill / heat index) */
    gdouble dew_point_c;        /* dew point temperature */
    gdouble humidity;
    gdouble wind_kph;
    gdouble wind_deg;
    gdouble wind_gust_kph;      /* gust speed in km/h */
    gdouble pressure_hpa;
    gdouble uv_index;
    gdouble cloud_fraction;     /* cloud area % (0-100) */
    gdouble fog_fraction;       /* fog area % — visibility proxy */
    gdouble precip_mm;          /* precipitation amount next 1h/6h */
    gchar  *condition;          /* "Partly cloudy" etc. */
    gchar  *icon_name;          /* e.g. "weather-few-clouds-symbolic" (GTK fallback) */
    gchar  *symbol_code;        /* raw met.no symbol_code for SVG lookup */
    gint64  timestamp;          /* Unix time of observation */
} WeatherCurrent;

typedef struct {
    gint64  time;
    gdouble temp_c;
    gchar  *icon_name;
    gchar  *symbol_code;        /* met.no symbol_code for SVG icon */
} WeatherHour;

typedef struct {
    gint64  date;
    gdouble temp_max_c;
    gdouble temp_min_c;
    gchar  *icon_name;
    gchar  *symbol_code;        /* met.no symbol_code for SVG icon */
} WeatherDay;

typedef struct {
    WeatherCurrent  current;
    WeatherHour     hourly[24];
    WeatherDay      daily[7];
    gint            hourly_count;
    gint            daily_count;
    gint64          sunrise;    /* Unix timestamp */
    gint64          sunset;     /* Unix timestamp */
} WeatherData;

typedef struct {
    gchar           *location;
    gdouble          lat, lon;
    gboolean         use_fahrenheit;
    gint             refresh_minutes;
    WeatherProvider  provider;
    gchar           *owm_api_key;
} WeatherConfig;
