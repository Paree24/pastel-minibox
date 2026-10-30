#!/usr/bin/env bash
# Installs Pastel Minibox for the current user. Run from a source checkout it builds (Release) first;
# run from an unpacked Linux release (Pastel-Minibox-Linux.tar.gz) it installs the prebuilt files as they are:
#   VST3        ~/.vst3/Minibox.vst3 (factory content inside the bundle)
#   Standalone  ~/.local/opt/pastel-minibox/ (binary + Factory next to it),
#               linked as ~/.local/bin/pastel-minibox
#   Linux menu  icon (hicolor theme) and ~/.local/share/applications/pastel-minibox.desktop
#
# Usage: ./install.sh [--no-build] [--vst3-only | --standalone-only] [--uninstall]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
CONFIG=Release

VST3_DIR="$HOME/.vst3"
APP_DIR="$HOME/.local/opt/pastel-minibox"
BIN_LINK="$HOME/.local/bin/pastel-minibox"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
ICONS="$DATA/icons/hicolor"
DESKTOP="$DATA/applications/pastel-minibox.desktop"
ICON_SVG="$ROOT/Assets/icon/pastel-minibox.svg"

# a release package carries the built files next to this script, and no sources
packaged=0
[ ! -f "$ROOT/CMakeLists.txt" ] && [ -d "$ROOT/Minibox.vst3" ] && packaged=1

build=1; vst3=1; app=1; uninstall=0
for arg in "$@"; do
    case "$arg" in
        --no-build)        build=0 ;;
        --vst3-only)       app=0 ;;
        --standalone-only) vst3=0 ;;
        --uninstall)       uninstall=1 ;;
        -h|--help)         sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $arg (see --help)" >&2; exit 2 ;;
    esac
done

refresh_menus() {
    command -v gtk-update-icon-cache  >/dev/null && gtk-update-icon-cache -q -t "$ICONS" 2>/dev/null || true
    command -v update-desktop-database >/dev/null && update-desktop-database -q "$DATA/applications" 2>/dev/null || true
    command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true
}

if [ "$uninstall" = 1 ]; then
    [ "$vst3" = 1 ] && rm -rf "$VST3_DIR/Minibox.vst3"
    if [ "$app" = 1 ]; then
        rm -rf "$APP_DIR"
        rm -f "$BIN_LINK" "$DESKTOP"
        find "$ICONS" -name 'pastel-minibox.*' -delete 2>/dev/null || true
        refresh_menus
    fi
    echo "Pastel Minibox removed (your presets, sessions and settings are kept)."
    exit 0
fi

[ "$packaged" = 1 ] && build=0   # nothing to build: the release ships the binaries

# ---- build
if [ "$build" = 1 ]; then
    if [ ! -f "$BUILD/CMakeCache.txt" ]; then
        gen=(); command -v ninja >/dev/null && gen=(-G Ninja)
        cmake -S "$ROOT" -B "$BUILD" "${gen[@]}" -DCMAKE_BUILD_TYPE=$CONFIG
    fi
    targets=(MiniboxFactoryContent)
    [ "$vst3" = 1 ] && targets+=(MiniboxPlugin_VST3)
    [ "$app" = 1 ] && targets+=(MiniboxStandalone)
    cmake --build "$BUILD" --config $CONFIG --target "${targets[@]}" -j"$(nproc 2>/dev/null || echo 4)"
fi

if [ "$packaged" = 1 ]; then
    VST3_SRC="$ROOT/Minibox.vst3"
    APP_SRC="$ROOT/Standalone"
else
    VST3_SRC="$BUILD/MiniboxPlugin_artefacts/$CONFIG/VST3/Minibox.vst3"
    APP_SRC="$BUILD/MiniboxStandalone_artefacts/$CONFIG"
fi

# ---- VST3
if [ "$vst3" = 1 ]; then
    [ -d "$VST3_SRC" ] || { echo "missing $VST3_SRC (build first)" >&2; exit 1; }
    mkdir -p "$VST3_DIR"
    rm -rf "$VST3_DIR/Minibox.vst3"
    cp -r "$VST3_SRC" "$VST3_DIR/"
    echo "VST3        -> $VST3_DIR/Minibox.vst3"
fi

# ---- Standalone + menu entry
if [ "$app" = 1 ]; then
    [ -x "$APP_SRC/Pastel Minibox" ] || { echo "missing $APP_SRC/Pastel Minibox (build first)" >&2; exit 1; }
    rm -rf "$APP_DIR"
    mkdir -p "$APP_DIR" "$(dirname "$BIN_LINK")"
    cp "$APP_SRC/Pastel Minibox" "$APP_DIR/"
    # the app looks for Factory next to its binary. A release package carries it once, inside the VST3 bundle
    FACTORY_SRC="$APP_SRC/Factory"
    [ -d "$FACTORY_SRC" ] || FACTORY_SRC="$VST3_SRC/Contents/Resources/Factory"
    [ -d "$FACTORY_SRC" ] || { echo "missing the Factory content (samples, presets)" >&2; exit 1; }
    cp -r "$FACTORY_SRC" "$APP_DIR/Factory"
    ln -sfn "$APP_DIR/Pastel Minibox" "$BIN_LINK"
    echo "Standalone  -> $APP_DIR (run: pastel-minibox)"

    mkdir -p "$ICONS/scalable/apps"
    cp "$ICON_SVG" "$ICONS/scalable/apps/pastel-minibox.svg"
    for s in 16 24 32 48 64 128 256 512; do
        mkdir -p "$ICONS/${s}x${s}/apps"
        if command -v rsvg-convert >/dev/null; then
            rsvg-convert -w $s -h $s "$ICON_SVG" -o "$ICONS/${s}x${s}/apps/pastel-minibox.png"
        elif command -v magick >/dev/null; then
            magick -background none -density 384 "$ICON_SVG" -resize ${s}x${s} "$ICONS/${s}x${s}/apps/pastel-minibox.png"
        fi
    done

    mkdir -p "$(dirname "$DESKTOP")"
    cat > "$DESKTOP" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Pastel Minibox
GenericName=Groovebox
Comment=Seven-device groovebox: Mono, Poly, Acid, FM, Sampler, Drums, Drum Sampler
Exec="$APP_DIR/Pastel Minibox"
Path=$APP_DIR
Icon=pastel-minibox
Terminal=false
Categories=AudioVideo;Audio;Music;Midi;
Keywords=groovebox;synth;sequencer;drums;sampler;music;
StartupWMClass=Pastel Minibox
StartupNotify=true
EOF
    chmod 644 "$DESKTOP"
    refresh_menus
    echo "Menu entry  -> $DESKTOP"
fi
