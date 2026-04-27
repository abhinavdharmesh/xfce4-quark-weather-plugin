#!/usr/bin/env bash
# install.sh — One-command installer for Quark Weather Plugin
# Usage: curl -fsSL https://raw.githubusercontent.com/abhinavdharmesh/xfce4-quark-weather-plugin/main/install.sh | bash
set -e

REPO_URL="https://github.com/abhinavdharmesh/xfce4-quark-weather-plugin"
CLONE_DIR="$HOME/.local/src/xfce4-quark-weather-plugin"

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║   Quark Weather Plugin — Installer       ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# ── Detect package manager and install build deps ──────────────────────
echo "==> Installing build dependencies…"

if command -v apt &>/dev/null; then
    sudo apt update -qq
    sudo apt install -y libxfce4panel-2.0-dev libxfce4util-dev libgtk-3-dev \
        libsoup2.4-dev libjson-glib-dev librsvg2-dev meson ninja-build git
elif command -v pacman &>/dev/null; then
    sudo pacman -S --needed --noconfirm xfce4-panel gtk3 libsoup \
        json-glib librsvg meson ninja git
elif command -v dnf &>/dev/null; then
    sudo dnf install -y xfce4-panel-devel gtk3-devel libsoup-devel \
        json-glib-devel librsvg2-devel meson ninja-build git
elif command -v zypper &>/dev/null; then
    sudo zypper install -y xfce4-panel-devel gtk3-devel libsoup-devel \
        json-glib-devel librsvg2-devel meson ninja git
else
    echo "ERROR: Unsupported package manager."
    echo "Please install these manually:"
    echo "  libxfce4panel-2.0-dev libxfce4util-dev libgtk-3-dev"
    echo "  libsoup2.4-dev libjson-glib-dev librsvg2-dev meson ninja-build git"
    exit 1
fi

# ── Clone or update ────────────────────────────────────────────────────
if [ -d "$CLONE_DIR/.git" ]; then
    echo ""
    echo "==> Updating existing clone…"
    git -C "$CLONE_DIR" pull --ff-only
else
    echo ""
    echo "==> Cloning repository…"
    mkdir -p "$(dirname "$CLONE_DIR")"
    git clone "$REPO_URL" "$CLONE_DIR"
fi

# ── Build ──────────────────────────────────────────────────────────────
echo ""
echo "==> Building…"
cd "$CLONE_DIR"
rm -rf build
meson setup build --prefix=/usr
ninja -C build

# ── Install ────────────────────────────────────────────────────────────
echo ""
echo "==> Installing (requires sudo)…"
sudo ninja -C build install

# ── Restart panel ──────────────────────────────────────────────────────
echo ""
echo "==> Restarting Xfce panel…"
xfce4-panel -r 2>/dev/null || true

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║  ✓ Quark Weather installed!              ║"
echo "║                                          ║"
echo "║  Right-click your panel → Add New Items  ║"
echo "║  → search 'Quark'                        ║"
echo "╚══════════════════════════════════════════╝"
echo ""
echo "To update later, run:"
echo "  cd $CLONE_DIR && git pull && ninja -C build && sudo ninja -C build install && xfce4-panel --quit ; xfce4-panel & 
 -r"
echo ""
