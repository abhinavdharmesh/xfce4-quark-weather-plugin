# Quark Weather

**A modern, glassmorphism-styled weather plugin for the Xfce panel.**  
Uses [met.no](https://api.met.no/) — **no API key required**. Works out of the box on any Xfce desktop.

## Features

- **Current conditions** — temperature, feels-like, humidity, wind, UV, dew point, pressure, visibility
- **24-hour** hourly forecast with scrollable cards
- **7-day** daily forecast with temperature range bars
- **Glassmorphism popup** with translucent dark glass and SVG weather icons
- **Free met.no data** — no account, no API key, no rate limits
- **wttr.in fallback** — seamlessly switches if met.no is unreachable
- **Location search** — type a city name, pick from results (Nominatim)
- **Auto-detect location** — one-click IP geolocation
- **Right-click menu** — Refresh Now, Change Location, Properties, About
- **Sunrise & sunset** with daylight duration

## Install

### Option A — One-liner (Ubuntu / Debian / Xubuntu / Linux Mint)

```bash
curl -fsSL https://raw.githubusercontent.com/abhinavdharmesh/xfce4-quark-weather-plugin/main/install.sh | bash
```

### Option B — Build from source

```bash
# Install build dependencies
sudo apt install -y libxfce4panel-2.0-dev libxfce4util-dev libgtk-3-dev \
    libsoup2.4-dev libjson-glib-dev librsvg2-dev meson ninja-build

# Clone and build
git clone https://github.com/abhinavdharmesh/xfce4-quark-weather-plugin
cd xfce4-quark-weather-plugin
meson setup build --prefix=/usr
ninja -C build
sudo ninja -C build install

# Restart the panel
xfce4-panel -r
```

Then right-click your panel → **Add New Items** → search **"Quark"**.

### Option C — Pre-built .deb (no compilation)

Download the `.deb` from the [Releases](https://github.com/abhinavdharmesh/xfce4-quark-weather-plugin/releases) page:

```bash
sudo dpkg -i xfce4-quark-weather-plugin_1.0.0_amd64.deb
sudo apt install -f   # fix any missing deps
xfce4-panel -r
```

## Usage

| Action | What it does |
|--------|-------------|
| **Left-click** the panel button | Opens the forecast popup |
| **Right-click** → Change Location… | Search and set your city |
| **Right-click** → Properties | Open all settings |
| **Right-click** → Refresh Now | Force a weather update |
| **Right-click** → About | Plugin info & version |

## Requirements

- Xfce **4.14** or later
- GTK **3.22+**
- libsoup **2.4**
- json-glib **1.0**
- librsvg2 (for SVG weather icons — usually already installed)
- glib-networking (for HTTPS — usually already installed)

## Weather Data

All weather data comes from the [Norwegian Meteorological Institute (met.no)](https://api.met.no/).  
No API key, no registration, no cost. Covers the entire world.

Sunrise/sunset data from met.no's [Sunrise API 3.0](https://api.met.no/weatherapi/sunrise/3.0/documentation).

Fallback provider: [wttr.in](https://wttr.in/) (also free, no key).

## License

GPL-2.0-or-later

---

Made with ☀ by [Abhinav Dharmesh](mailto:abhinavdharmesh@dei.ac.in)  
Dayalbagh Educational Institute, Agra, India
