#!/usr/bin/env bash
#
# install.sh
# AudioKit Synth One - Linux port
#
# Installs the binaries, their runtime data and a desktop menu entry.
#
# The binaries have the source-tree path compiled in as a fallback, so a bare
# copy of the executable would break the moment the checkout moved. This script
# therefore installs the wavetables, preset banks and tuning library alongside
# them and puts thin wrappers on PATH that point the binaries at the installed
# copies. Arguments you pass are appended, so `--resources` and friends still
# override the defaults.
#
#   ./install.sh                 # install to ~/.local
#   ./install.sh --prefix /usr/local   # system-wide (needs root)
#   ./install.sh --uninstall
#

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

APP_ID="synthone"
APP_NAME="AudioKit Synth One"
BINARIES=(synthone-gui synthone synthone-offline)
ICON_SIZES=(512 256 128 64 48 32)

# The script runs in two layouts:
#
#   package  - unpacked distribution archive; binaries and data sit beside it
#   repo     - a source checkout; binaries come from ./build, data from the
#              iOS source tree
#
# PAYLOAD_* point at whichever applies.
if [ -d "$HERE/bin" ] && [ -d "$HERE/share/$APP_ID" ]; then
    MODE="package"
    PAYLOAD_BIN="$HERE/bin"
    PAYLOAD_DATA="$HERE/share/$APP_ID"
    ICON_SRC="$HERE/share/icon.png"
else
    MODE="repo"
    REPO="$(cd -- "$HERE/.." && pwd)"
    RESOURCES="$REPO/AudioKitSynthOne"
    PAYLOAD_BIN="$HERE/build"
    PAYLOAD_DATA=""   # assembled from the source tree below
    ICON_SRC="$RESOURCES/Assets/Assets.xcassets/AppIcon.appiconset/Icon 512.png"
fi

if [ "$(id -u)" -eq 0 ]; then
    PREFIX="/usr/local"
else
    PREFIX="$HOME/.local"
fi
ACTION="install"

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)    PREFIX="${2:?--prefix needs a directory}"; shift 2 ;;
        --prefix=*)  PREFIX="${1#*=}"; shift ;;
        --uninstall) ACTION="uninstall"; shift ;;
        -h|--help)
            sed -n '3,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

BIN_DIR="$PREFIX/bin"
LIB_DIR="$PREFIX/lib/$APP_ID"
DATA_DIR="$PREFIX/share/$APP_ID"
DESKTOP_DIR="$PREFIX/share/applications"
ICON_ROOT="$PREFIX/share/icons/hicolor"

say()  { printf '  %s\n' "$*"; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

refresh_caches() {
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -qtf "$ICON_ROOT" 2>/dev/null || true
    fi
}

# ---------------------------------------------------------------------------
# Uninstall
# ---------------------------------------------------------------------------

if [ "$ACTION" = uninstall ]; then
    echo "Uninstalling $APP_NAME from $PREFIX"
    for b in "${BINARIES[@]}"; do
        rm -f "$BIN_DIR/$b" && say "removed $BIN_DIR/$b" || true
    done
    rm -rf "$LIB_DIR"  && say "removed $LIB_DIR"
    rm -rf "$DATA_DIR" && say "removed $DATA_DIR"
    rm -f  "$DESKTOP_DIR/$APP_ID.desktop" && say "removed the menu entry"
    for size in "${ICON_SIZES[@]}"; do
        rm -f "$ICON_ROOT/${size}x${size}/apps/$APP_ID.png"
    done
    say "removed icons"
    refresh_caches
    echo
    echo "Done. Your presets in \${XDG_DATA_HOME:-\$HOME/.local/share}/$APP_ID/presets were kept."
    exit 0
fi

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

echo "Installing $APP_NAME to $PREFIX"

say "source: $MODE layout"

# In a checkout, build first if there is nothing to install yet.
if [ "$MODE" = repo ] && [ ! -x "$PAYLOAD_BIN/synthone-gui" ]; then
    say "no build found, configuring and building"
    command -v cmake >/dev/null 2>&1 || die "cmake is required to build"
    cmake -S "$HERE" -B "$PAYLOAD_BIN" -G Ninja >/dev/null
    cmake --build "$PAYLOAD_BIN" >/dev/null
fi

for b in "${BINARIES[@]}"; do
    [ -x "$PAYLOAD_BIN/$b" ] || die "missing $PAYLOAD_BIN/$b"
done

install -d "$BIN_DIR" "$LIB_DIR" "$DATA_DIR" "$DESKTOP_DIR"

# -- binaries ---------------------------------------------------------------

for b in "${BINARIES[@]}"; do
    install -m 755 "$PAYLOAD_BIN/$b" "$LIB_DIR/$b"
done
say "binaries -> $LIB_DIR"

# -- runtime data -----------------------------------------------------------
#
# Only what the engine actually reads: the bandlimited wavetables, the factory
# preset banks and the generated tuning library (~8 MB in total).

if [ "$MODE" = package ]; then
    cp -r "$PAYLOAD_DATA/." "$DATA_DIR/"
else
    [ -d "$RESOURCES/DSP/BandlimitedWavetables" ] || die "cannot find wavetables under $RESOURCES"
    install -d "$DATA_DIR/DSP" "$DATA_DIR/Presets"
    cp -r "$RESOURCES/DSP/BandlimitedWavetables" "$DATA_DIR/DSP/"
    cp -r "$RESOURCES/Presets/Data" "$DATA_DIR/Presets/"
    if [ -f "$HERE/data/tunings.json" ]; then
        install -m 644 "$HERE/data/tunings.json" "$DATA_DIR/tunings.json"
    else
        say "warning: data/tunings.json missing; the tuning library will be empty"
    fi
fi
say "runtime data -> $DATA_DIR ($(du -sh "$DATA_DIR" | cut -f1))"

# -- wrappers ---------------------------------------------------------------
#
# Defaults are placed before "$@" so anything the caller passes wins.

for b in "${BINARIES[@]}"; do
    # `synthone` (the headless host) has no tuning library to load.
    case "$b" in
        synthone) extra="" ;;
        *)        extra="--tunings \"$DATA_DIR/tunings.json\"" ;;
    esac
    cat > "$BIN_DIR/$b" <<WRAPPER
#!/bin/sh
# Generated by AudioKit Synth One install.sh -- do not edit.
exec "$LIB_DIR/$b" --resources "$DATA_DIR" $extra "\$@"
WRAPPER
    chmod 755 "$BIN_DIR/$b"
done
say "wrappers -> $BIN_DIR"

# -- icons ------------------------------------------------------------------

if [ -f "$ICON_SRC" ]; then
    for size in "${ICON_SIZES[@]}"; do
        install -d "$ICON_ROOT/${size}x${size}/apps"
        if command -v ffmpeg >/dev/null 2>&1; then
            ffmpeg -y -loglevel error -i "$ICON_SRC" -vf "scale=$size:$size" \
                "$ICON_ROOT/${size}x${size}/apps/$APP_ID.png"
        elif command -v convert >/dev/null 2>&1; then
            convert "$ICON_SRC" -resize "${size}x${size}" \
                "$ICON_ROOT/${size}x${size}/apps/$APP_ID.png"
        else
            install -m 644 "$ICON_SRC" "$ICON_ROOT/${size}x${size}/apps/$APP_ID.png"
        fi
    done
    say "icons -> $ICON_ROOT"
else
    say "warning: no icon at $ICON_SRC; the menu entry will use a generic icon"
fi

# -- desktop entry ----------------------------------------------------------

cat > "$DESKTOP_DIR/$APP_ID.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Version=1.0
Name=$APP_NAME
GenericName=Synthesizer
Comment=Polyphonic synthesizer - AudioKit Synth One, Linux port
Exec=$BIN_DIR/synthone-gui
Icon=$APP_ID
Terminal=false
Categories=AudioVideo;Audio;Music;
Keywords=synth;synthesizer;midi;jack;audio;music;
StartupNotify=true
DESKTOP
chmod 644 "$DESKTOP_DIR/$APP_ID.desktop"
say "menu entry -> $DESKTOP_DIR/$APP_ID.desktop"

refresh_caches

# -- report -----------------------------------------------------------------

echo
echo "Done."
echo "  Launch from your application menu, or run: synthone-gui"
echo "  Headless host: synthone --backend jack --bank \"Starter Bank\""
echo "  Presets you save go to \${XDG_DATA_HOME:-\$HOME/.local/share}/$APP_ID/presets"

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *) echo
       echo "  NOTE: $BIN_DIR is not on your PATH. Add it with:"
       echo "      export PATH=\"$BIN_DIR:\$PATH\"" ;;
esac
