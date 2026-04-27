#pragma once
#include <libxfce4panel/libxfce4panel.h>
#include "weather-data.h"

void weather_config_load(WeatherConfig *cfg);
void weather_config_save(const WeatherConfig *cfg);
void weather_config_dialog(XfcePanelPlugin *plugin, gpointer ud);
