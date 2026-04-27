#include "weather-api.h"
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ══════════════════════════════════════════════════════════════════════
 * Soup session
 * ══════════════════════════════════════════════════════════════════════ */
static SoupSession *g_session = NULL;

static SoupSession *get_session(void) {
    if (!g_session)
        g_session = soup_session_new_with_options(
            /* met.no ToS require an identifiable User-Agent with contact */
            SOUP_SESSION_USER_AGENT,
                "QuarkWeather/1.0 "
                "(Contact: abhinavdharmesh@dei.ac.in; "
                "https://github.com/abhinavdharmesh/xfce4-quark-weather-plugin)",
            SOUP_SESSION_TIMEOUT, 20,
            NULL);
    return g_session;
}

/* Cancel all in-flight requests.  Call from plugin destructor to avoid
 * use-after-free if the plugin is removed while a fetch is in progress. */
void weather_api_cancel_all(void) {
    if (g_session) soup_session_abort(g_session);
}

/* ══════════════════════════════════════════════════════════════════════
 * Error domain
 * ══════════════════════════════════════════════════════════════════════ */
#define WEATHER_ERROR (g_quark_from_static_string("weather-api"))
typedef enum { ERR_NETWORK = 1, ERR_PARSE, ERR_RATE_LIMIT } WeatherErrorCode;

/* ══════════════════════════════════════════════════════════════════════
 * Safe JSON accessors (all NULL-safe)
 * ══════════════════════════════════════════════════════════════════════ */
static JsonObject *jobj(JsonObject *o, const gchar *k) {
    if (!o || !json_object_has_member(o, k)) return NULL;
    JsonNode *n = json_object_get_member(o, k);
    return (n && JSON_NODE_HOLDS_OBJECT(n)) ? json_node_get_object(n) : NULL;
}
static JsonArray *jarr(JsonObject *o, const gchar *k) {
    if (!o || !json_object_has_member(o, k)) return NULL;
    JsonNode *n = json_object_get_member(o, k);
    return (n && JSON_NODE_HOLDS_ARRAY(n)) ? json_node_get_array(n) : NULL;
}
static gdouble jdbl(JsonObject *o, const gchar *k) {
    if (!o || !json_object_has_member(o, k)) return 0.0;
    return json_object_get_double_member(o, k);
}
static const gchar *jstr(JsonObject *o, const gchar *k) {
    if (!o || !json_object_has_member(o, k)) return NULL;
    return json_object_get_string_member(o, k);
}
/* wttr.in encodes numbers as strings */
static gdouble jstrnum(JsonObject *o, const gchar *k) {
    const gchar *v = jstr(o, k);
    return v ? g_ascii_strtod(v, NULL) : 0.0;
}

/* ══════════════════════════════════════════════════════════════════════
 * Derived fields
 * ══════════════════════════════════════════════════════════════════════ */
static gdouble compute_feels_like(gdouble temp_c, gdouble wind_kph, gdouble hum) {
    if (temp_c < 10.0 && wind_kph > 4.8) {
        gdouble v = pow(wind_kph, 0.16);
        return 13.12 + 0.6215*temp_c - 11.37*v + 0.3965*temp_c*v;
    }
    if (temp_c > 27.0 && hum > 40.0) {
        gdouble T = temp_c*9.0/5.0 + 32.0, H = hum;
        gdouble hi = -42.379 + 2.04901523*T + 10.14333127*H
            - 0.22475541*T*H - 0.00683783*T*T - 0.05481717*H*H
            + 0.00122874*T*T*H + 0.00085282*T*H*H - 0.00000199*T*T*H*H;
        return (hi - 32.0) * 5.0/9.0;
    }
    return temp_c;
}

static const gchar *sym_to_condition(const gchar *s) {
    if (!s) return "Unknown";
    static const struct { const char *p, *l; } m[] = {
        {"heavyrainandthunder","Heavy Rain & Thunder"},{"heavysnowandthunder","Heavy Snow & Thunder"},
        {"heavysleetandthunder","Heavy Sleet & Thunder"},{"lightrainandthunder","Light Rain & Thunder"},
        {"rainandthunder","Rain & Thunder"},{"sleetandthunder","Sleet & Thunder"},
        {"snowandthunder","Snow & Thunder"},{"heavyrain","Heavy Rain"},{"lightrain","Light Rain"},
        {"heavysnow","Heavy Snow"},{"lightsnow","Light Snow"},{"partlycloudy","Partly Cloudy"},
        {"clearsky","Clear Sky"},{"fair","Fair"},{"cloudy","Cloudy"},
        {"fog","Foggy"},{"rain","Rain"},{"sleet","Sleet"},{"snow","Snow"},{NULL,NULL}
    };
    for (int i = 0; m[i].p; i++) if (g_str_has_prefix(s, m[i].p)) return m[i].l;
    return s;
}

/* ══════════════════════════════════════════════════════════════════════
 * 7-day daily bucketing from met.no timeseries
 * ══════════════════════════════════════════════════════════════════════ */
static void bucket_daily(JsonArray *ts, WeatherData *out) {
    typedef struct { gint ymd; gint64 unix; gdouble tmin, tmax;
                     gchar *sym; gboolean noon; } Bkt;
    Bkt b[7]; memset(b, 0, sizeof(b));
    gint nb = 0;
    guint len = json_array_get_length(ts);

    for (guint i = 0; i < len; i++) {
        JsonObject *slot = json_array_get_object_element(ts, i);
        const gchar *ts_str = jstr(slot, "time");
        if (!ts_str) continue;
        GDateTime *dt = g_date_time_new_from_iso8601(ts_str, NULL);
        if (!dt) continue;
        gint ymd  = g_date_time_get_year(dt)*10000
                  + g_date_time_get_month(dt)*100
                  + g_date_time_get_day_of_month(dt);
        gint hour = g_date_time_get_hour(dt);
        gint64 ut = g_date_time_to_unix(dt);
        g_date_time_unref(dt);

        gdouble t = jdbl(jobj(jobj(slot, "data"), "instant"), "air_temperature");
        /* workaround: jobj chain needs intermediary */
        JsonObject *sd = jobj(slot, "data");
        JsonObject *si = jobj(sd, "instant");
        JsonObject *sdet = jobj(si, "details");
        t = jdbl(sdet, "air_temperature");

        gint bi = -1;
        for (gint k = 0; k < nb; k++) if (b[k].ymd == ymd) { bi = k; break; }
        if (bi < 0) { if (nb >= 7) continue; bi = nb++; b[bi].ymd=ymd; b[bi].unix=ut; b[bi].tmin=t; b[bi].tmax=t; }
        if (t < b[bi].tmin) b[bi].tmin = t;
        if (t > b[bi].tmax) b[bi].tmax = t;

        if (!b[bi].noon && hour >= 11 && hour <= 14) {
            JsonObject *h6 = jobj(sd, "next_6_hours");
            JsonObject *h1 = jobj(sd, "next_1_hours");
            const gchar *s = jstr(jobj(h6 ? h6 : h1, "summary"), "symbol_code");
            if (s) { g_free(b[bi].sym); b[bi].sym = g_strdup(s); b[bi].noon = (hour==12); }
        }
    }

    out->daily_count = nb;
    for (gint i = 0; i < nb; i++) {
        out->daily[i].date        = b[i].unix;
        out->daily[i].temp_min_c  = b[i].tmin;
        out->daily[i].temp_max_c  = b[i].tmax;
        out->daily[i].symbol_code = b[i].sym;   /* ownership transfer */
        out->daily[i].icon_name   = b[i].sym
            ? g_strdup_printf("weather-%s-symbolic", b[i].sym)
            : g_strdup("weather-overcast-symbolic");
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * met.no parser  — uses /complete endpoint for all fields
 * ══════════════════════════════════════════════════════════════════════ */
static gboolean parse_metno(JsonParser *jp, WeatherData *out, GError **err) {
    JsonNode *root = json_parser_get_root(jp);
    if (!root) { g_set_error(err,WEATHER_ERROR,ERR_PARSE,"Empty response"); return FALSE; }
    JsonObject *ro = json_node_get_object(root);
    JsonArray  *ts = jarr(jobj(ro, "properties"), "timeseries");
    if (!ts || json_array_get_length(ts) == 0) {
        g_set_error(err,WEATHER_ERROR,ERR_PARSE,"No timeseries"); return FALSE;
    }

    /* Current conditions */
    JsonObject *f0   = json_array_get_object_element(ts, 0);
    JsonObject *d0   = jobj(f0, "data");
    JsonObject *det0 = jobj(jobj(d0, "instant"), "details");
    if (!det0) { g_set_error(err,WEATHER_ERROR,ERR_PARSE,"Missing details"); return FALSE; }

    WeatherCurrent *c = &out->current;
    c->temp_c         = jdbl(det0, "air_temperature");
    c->humidity       = jdbl(det0, "relative_humidity");
    c->wind_kph       = jdbl(det0, "wind_speed") * 3.6;
    c->wind_deg       = jdbl(det0, "wind_from_direction");
    c->pressure_hpa   = jdbl(det0, "air_pressure_at_sea_level");
    c->uv_index       = jdbl(det0, "ultraviolet_index_clear_sky");
    c->dew_point_c    = jdbl(det0, "dew_point_temperature");
    c->wind_gust_kph  = jdbl(det0, "wind_speed_of_gust") * 3.6;
    c->fog_fraction   = jdbl(det0, "fog_area_fraction");
    c->cloud_fraction = jdbl(det0, "cloud_area_fraction");
    c->timestamp      = g_get_real_time() / G_USEC_PER_SEC;
    c->feels_like_c   = compute_feels_like(c->temp_c, c->wind_kph, c->humidity);

    JsonObject *n1 = jobj(d0, "next_1_hours");
    JsonObject *n6 = jobj(d0, "next_6_hours");
    JsonObject *nsrc = n1 ? n1 : n6;
    const gchar *sym = jstr(jobj(nsrc, "summary"), "symbol_code");
    if (!sym) sym = "cloudy";
    c->precip_mm = jdbl(jobj(nsrc, "details"), "precipitation_amount");

    g_free(c->symbol_code); c->symbol_code = g_strdup(sym);
    g_free(c->icon_name);   c->icon_name   = g_strdup_printf("weather-%s-symbolic", sym);
    g_free(c->condition);   c->condition   = g_strdup(sym_to_condition(sym));

    /* Hourly (24 slots) */
    guint ts_len = json_array_get_length(ts);
    out->hourly_count = (gint)MIN(ts_len, 24);
    for (gint i = 0; i < out->hourly_count; i++) {
        JsonObject *slot = json_array_get_object_element(ts, i);
        JsonObject *sd   = jobj(slot, "data");
        JsonObject *sdet = jobj(jobj(sd, "instant"), "details");
        const gchar *tstr = jstr(slot, "time");
        if (tstr) {
            GDateTime *dt = g_date_time_new_from_iso8601(tstr, NULL);
            out->hourly[i].time = dt ? g_date_time_to_unix(dt) : 0;
            if (dt) g_date_time_unref(dt);
        }
        out->hourly[i].temp_c = jdbl(sdet, "air_temperature");
        JsonObject *sh = jobj(sd, "next_1_hours");
        if (!sh) sh = jobj(sd, "next_6_hours");
        const gchar *hs = jstr(jobj(sh, "summary"), "symbol_code");
        if (!hs) hs = "cloudy";
        g_free(out->hourly[i].symbol_code); out->hourly[i].symbol_code = g_strdup(hs);
        g_free(out->hourly[i].icon_name);   out->hourly[i].icon_name   = g_strdup_printf("weather-%s-symbolic", hs);
    }

    bucket_daily(ts, out);
    return TRUE;
}

/* ══════════════════════════════════════════════════════════════════════
 * wttr.in parser
 * ══════════════════════════════════════════════════════════════════════ */
static const gchar *wwo_to_sym(gint code) {
    switch(code) {
    case 113: return "clearsky_day"; case 116: return "partlycloudy_day";
    case 119: case 122: return "cloudy"; case 143: case 248: case 260: return "fog";
    case 176: case 293: case 296: case 353: return "lightrain";
    case 200: case 386: case 389: return "rainandthunder";
    case 179: case 323: case 326: case 368: case 371: return "lightsnow";
    case 182: case 185: case 317: case 320: case 362: case 365: return "sleet";
    case 227: case 329: case 332: case 335: case 338: case 377: return "snow";
    case 230: return "heavysnow"; case 263: case 266: return "lightrain";
    case 281: case 284: return "sleet"; case 299: case 302: return "rain";
    case 305: case 308: case 356: case 359: return "heavyrain";
    case 311: case 314: return "sleet"; case 392: case 395: return "snowandthunder";
    default: return "cloudy";
    }
}

static gboolean parse_wttrin(JsonParser *jp, WeatherData *out, GError **err) {
    JsonNode *root = json_parser_get_root(jp);
    if (!root) { g_set_error(err,WEATHER_ERROR,ERR_PARSE,"Empty response"); return FALSE; }
    JsonObject *obj = json_node_get_object(root);
    JsonArray *cc_arr = jarr(obj, "current_condition");
    if (!cc_arr || json_array_get_length(cc_arr) == 0) {
        g_set_error(err,WEATHER_ERROR,ERR_PARSE,"No current_condition"); return FALSE;
    }
    JsonObject *cc = json_array_get_object_element(cc_arr, 0);
    WeatherCurrent *c = &out->current;

    c->temp_c       = jstrnum(cc, "temp_C");
    c->humidity     = jstrnum(cc, "humidity");
    c->wind_kph     = jstrnum(cc, "windspeedKmph");
    c->wind_deg     = jstrnum(cc, "winddirDegree");
    c->pressure_hpa = jstrnum(cc, "pressure");
    c->feels_like_c = jstrnum(cc, "FeelsLikeC");
    c->uv_index     = jstrnum(cc, "uvIndex");
    c->precip_mm    = jstrnum(cc, "precipMM");
    gdouble vis_km  = jstrnum(cc, "visibility");
    c->fog_fraction = (vis_km > 0) ? CLAMP(100.0 - vis_km*10.0, 0.0, 100.0) : 0.0;
    c->dew_point_c  = c->temp_c - (100.0 - c->humidity) / 5.0; /* Magnus approx */
    c->timestamp    = g_get_real_time() / G_USEC_PER_SEC;

    gint wcode = (gint)jstrnum(cc, "weatherCode");
    const gchar *sym = wwo_to_sym(wcode);
    g_free(c->symbol_code); c->symbol_code = g_strdup(sym);
    g_free(c->icon_name);   c->icon_name   = g_strdup_printf("weather-%s-symbolic", sym);
    JsonArray *da = jarr(cc, "weatherDesc");
    g_free(c->condition);
    c->condition = (da && json_array_get_length(da) > 0)
        ? g_strdup(jstr(json_array_get_object_element(da, 0), "value"))
        : g_strdup(sym_to_condition(sym));

    /* Hourly + daily */
    JsonArray *days = jarr(obj, "weather");
    if (days) {
        gint nh = 0;
        guint nd = MIN(json_array_get_length(days), 7);
        out->daily_count = (gint)nd;
        for (guint di = 0; di < nd; di++) {
            JsonObject *day = json_array_get_object_element(days, di);
            out->daily[di].temp_max_c = jstrnum(day, "maxtempC");
            out->daily[di].temp_min_c = jstrnum(day, "mintempC");
            const gchar *ds = jstr(day, "date");
            if (ds) {
                gchar *iso = g_strdup_printf("%sT12:00:00+00:00", ds);
                GDateTime *dt = g_date_time_new_from_iso8601(iso, NULL);
                out->daily[di].date = dt ? g_date_time_to_unix(dt) : 0;
                if (dt) g_date_time_unref(dt);
                g_free(iso);
            }
            JsonArray *hl = jarr(day, "hourly");
            /* noon icon: slot 4 = 1200 */
            if (hl && json_array_get_length(hl) > 4) {
                JsonObject *noon = json_array_get_object_element(hl, 4);
                const gchar *ds2 = wwo_to_sym((gint)jstrnum(noon, "weatherCode"));
                g_free(out->daily[di].symbol_code); out->daily[di].symbol_code = g_strdup(ds2);
                g_free(out->daily[di].icon_name);   out->daily[di].icon_name   = g_strdup_printf("weather-%s-symbolic", ds2);
            }
            /* Hourly slots */
            if (hl) {
                guint nslots = json_array_get_length(hl);
                for (guint si = 0; si < nslots && nh < 24; si++) {
                    JsonObject *h = json_array_get_object_element(hl, si);
                    gint tval = (gint)jstrnum(h, "time"); /* 0,300,600...2100 */
                    out->hourly[nh].temp_c = jstrnum(h, "tempC");
                    const gchar *hs = wwo_to_sym((gint)jstrnum(h, "weatherCode"));
                    g_free(out->hourly[nh].symbol_code); out->hourly[nh].symbol_code = g_strdup(hs);
                    g_free(out->hourly[nh].icon_name);   out->hourly[nh].icon_name   = g_strdup_printf("weather-%s-symbolic", hs);
                    /* Unix time: date + hour offset */
                    if (jstr(day, "date")) {
                        gchar *iso = g_strdup_printf("%sT%02d:%02d:00+05:30",
                            jstr(day, "date"), tval/100, tval%100);
                        GDateTime *dt = g_date_time_new_from_iso8601(iso, NULL);
                        out->hourly[nh].time = dt ? g_date_time_to_unix(dt) : 0;
                        if (dt) g_date_time_unref(dt);
                        g_free(iso);
                    }
                    nh++;
                }
            }
        }
        out->hourly_count = nh;
    }
    return TRUE;
}

/* ══════════════════════════════════════════════════════════════════════
 * Async plumbing — proven stable single-phase (same as working version)
 * Two separate fetch calls are made: forecast + sunrise (independent).
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    WeatherCallback  cb;
    gpointer         ud;
    WeatherProvider  prov;
} FetchCtx;

typedef struct {
    FetchCtx *fctx;
    gchar    *body;
    gint      status;
} IdleData;

/* Sunrise fetch is fire-and-forget: we keep a reference to the allocated
 * WeatherData. It's already been delivered to the UI; we just patch in
 * the sunrise/sunset fields and queue a redraw via a second idle callback. */
typedef struct {
    WeatherData **data_ptr;   /* pointer to wp->cached in plugin */
    gpointer      plugin_ud;  /* passed back so UI can redraw */
    void        (*redraw_cb)(gpointer);
} SunCtx;

typedef struct {
    SunCtx *sc;
    gchar  *body;
    gint    status;
} SunIdle;

static gboolean deliver_sunrise(gpointer p) {
    SunIdle *si = p;
    if (si->status >= 200 && si->status < 300 && si->body && *si->body
        && si->sc->data_ptr && *si->sc->data_ptr) {
        JsonParser *jp = json_parser_new();
        if (json_parser_load_from_data(jp, si->body, -1, NULL)) {
            JsonNode *root = json_parser_get_root(jp);
            if (root && JSON_NODE_HOLDS_OBJECT(root)) {
                JsonObject *ro = json_node_get_object(root);
                JsonObject *props = NULL;
                if (json_object_has_member(ro, "properties"))
                    props = json_object_get_object_member(ro, "properties");
                WeatherData *wd = *si->sc->data_ptr;
                if (props && wd) {
                    /* sunrise */
                    if (json_object_has_member(props, "sunrise")) {
                        JsonObject *sr = json_object_get_object_member(props, "sunrise");
                        const gchar *ts = jstr(sr, "time");
                        if (ts) {
                            GDateTime *dt = g_date_time_new_from_iso8601(ts, NULL);
                            if (dt) { wd->sunrise = g_date_time_to_unix(dt); g_date_time_unref(dt); }
                        }
                    }
                    /* sunset */
                    if (json_object_has_member(props, "sunset")) {
                        JsonObject *ss = json_object_get_object_member(props, "sunset");
                        const gchar *ts = jstr(ss, "time");
                        if (ts) {
                            GDateTime *dt = g_date_time_new_from_iso8601(ts, NULL);
                            if (dt) { wd->sunset = g_date_time_to_unix(dt); g_date_time_unref(dt); }
                        }
                    }
                }
            }
        }
        g_object_unref(jp);
        /* Ask plugin to redraw with updated sunrise/sunset */
        if (si->sc->redraw_cb) si->sc->redraw_cb(si->sc->plugin_ud);
    }
    g_free(si->body);
    g_free(si->sc);
    g_free(si);
    return G_SOURCE_REMOVE;
}

static void on_sunrise_resp(SoupSession *s, SoupMessage *msg, gpointer p) {
    (void)s;
    SunIdle *si = g_new0(SunIdle, 1);
    si->sc     = p;
    si->status = msg->status_code;
    si->body   = g_strndup(msg->response_body->data, msg->response_body->length);
    g_idle_add(deliver_sunrise, si);
}

static gboolean deliver_on_main(gpointer ud) {
    IdleData    *id  = ud;
    WeatherData *wd  = NULL;
    GError      *err = NULL;

    if (id->status >= 200 && id->status < 300) {
        wd = g_new0(WeatherData, 1);
        JsonParser *jp = json_parser_new();
        if (json_parser_load_from_data(jp, id->body, -1, &err)) {
            gboolean ok = (id->fctx->prov == PROVIDER_WTTRIN)
                ? parse_wttrin(jp, wd, &err)
                : parse_metno(jp, wd, &err);
            if (!ok && !err)
                g_set_error(&err, WEATHER_ERROR, ERR_PARSE, "Parser failed");
        }
        g_object_unref(jp);
    } else {
        g_set_error(&err, WEATHER_ERROR, ERR_NETWORK, "HTTP %d", id->status);
    }

    if (err && wd) { g_free(wd); wd = NULL; }
    id->fctx->cb(wd, err, id->fctx->ud);
    if (err) g_error_free(err);
    g_free(id->body); g_free(id->fctx); g_free(id);
    return G_SOURCE_REMOVE;
}

static void on_response(SoupSession *s, SoupMessage *msg, gpointer ud) {
    (void)s;
    IdleData *id = g_new0(IdleData, 1);
    id->fctx   = ud;
    id->status = msg->status_code;
    id->body   = g_strndup(msg->response_body->data, msg->response_body->length);
    g_idle_add(deliver_on_main, id);
}

/* ══════════════════════════════════════════════════════════════════════
 * Public API
 * ══════════════════════════════════════════════════════════════════════ */
void weather_api_fetch(const WeatherConfig *cfg, WeatherCallback cb, gpointer ud) {
    if (!cfg || !cb) return;
    FetchCtx *fctx = g_new0(FetchCtx, 1);
    fctx->cb = cb; fctx->ud = ud; fctx->prov = cfg->provider;

    gchar *url = NULL;
    switch (cfg->provider) {
    case PROVIDER_WTTRIN:
        url = g_strdup_printf("https://wttr.in/%s?format=j1",
            cfg->location ? cfg->location : "Agra");
        break;
    case PROVIDER_OWM:
        if (cfg->owm_api_key && *cfg->owm_api_key) {
            url = g_strdup_printf(
                "https://api.openweathermap.org/data/2.5/weather"
                "?lat=%.4f&lon=%.4f&appid=%s&units=metric",
                cfg->lat, cfg->lon, cfg->owm_api_key);
            break;
        }
        fctx->prov = PROVIDER_METNO;
        /* fall through */
    default:
    case PROVIDER_METNO:
        /* /complete gives dew_point, wind_gust, fog, cloud — use it */
        url = g_strdup_printf(
            "https://api.met.no/weatherapi/locationforecast/2.0/complete"
            "?lat=%.4f&lon=%.4f", cfg->lat, cfg->lon);
        break;
    }

    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);
    if (!msg) { g_free(fctx); return; }
    soup_message_headers_append(msg->request_headers, "Accept", "application/json");
    soup_session_queue_message(get_session(), msg, on_response, fctx);
}

void weather_api_fetch_sunrise(gdouble lat, gdouble lon,
                                WeatherData      **data_ptr,
                                void             (*redraw_cb)(gpointer),
                                gpointer           plugin_ud) {
    GDateTime *now = g_date_time_new_now_local();
    gchar *date = g_date_time_format(now, "%Y-%m-%d");
    g_date_time_unref(now);

    gchar *url = g_strdup_printf(
        "https://api.met.no/weatherapi/sunrise/3.0/sun"
        "?lat=%.4f&lon=%.4f&date=%s", lat, lon, date);
    g_free(date);

    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);
    if (!msg) return;
    soup_message_headers_append(msg->request_headers, "Accept", "application/json");

    SunCtx *sc = g_new0(SunCtx, 1);
    sc->data_ptr  = data_ptr;
    sc->redraw_cb = redraw_cb;
    sc->plugin_ud = plugin_ud;
    soup_session_queue_message(get_session(), msg, on_sunrise_resp, sc);
}
