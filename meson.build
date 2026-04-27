project('xfce4-quark-weather-plugin', 'c',
  version : '1.0.0',
  license : 'GPL-2.0-or-later',
  default_options : ['c_std=c11', 'warning_level=2'])

cc = meson.get_compiler('c')

# ── Dependencies ────────────────────────────────────────────────────────
gtk3       = dependency('gtk+-3.0',          version : '>= 3.22')
panel      = dependency('libxfce4panel-2.0', version : '>= 4.14')
xfce_util  = dependency('libxfce4util-1.0')
soup       = dependency('libsoup-2.4')
json_glib  = dependency('json-glib-1.0')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
libm       = cc.find_library('m', required : true)

# librsvg2 is only needed at runtime (as a GdkPixbuf SVG loader).
# We don't link against it directly — gdk-pixbuf-2.0 is sufficient.

# ── Plugin sources ──────────────────────────────────────────────────────
plugin_sources = files(
  'src/weather-plugin.c',
  'src/weather-api.c',
  'src/weather-config.c',
  'src/weather-ui.c',
  'src/weather-icons.c',
  'src/weather-search.c',
)

# ── Shared module (XFCE panel plugin) ──────────────────────────────────
shared_module('xfce4-quark-weather-plugin',
  plugin_sources,
  dependencies : [gtk3, panel, xfce_util, soup, json_glib, gdk_pixbuf, libm],
  install      : true,
  install_dir  : get_option('libdir') / 'xfce4' / 'panel-plugins',
  name_prefix  : 'lib',
)

# ── Desktop entry (tells xfce4-panel about the plugin) ─────────────────
install_data(
  'data/xfce4-quark-weather-plugin.desktop',
  install_dir : get_option('datadir') / 'xfce4' / 'panel' / 'plugins',
)

# ── Bundle met.no SVG weather icons ────────────────────────────────────
install_subdir('icons',
  install_dir : get_option('datadir') / 'xfce4' / 'weather-plugin',
)
