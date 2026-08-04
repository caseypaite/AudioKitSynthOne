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
# Raspberry Pi kiosk:
#
#   sudo ./install.sh --kiosk          # also boot straight into the synth
#   sudo ./install.sh --kiosk --kiosk-user pi
#
# --kiosk adds a systemd service that starts a bare X server on vt1 with
# synthone-gui as its only client: no desktop and no window manager. It needs
# root, because it takes tty1 away from getty. Settings live in
# /etc/synthone/kiosk.conf, not in the unit.
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
KIOSK=0
KIOSK_USER=""

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)    PREFIX="${2:?--prefix needs a directory}"; shift 2 ;;
        --prefix=*)  PREFIX="${1#*=}"; shift ;;
        --uninstall) ACTION="uninstall"; shift ;;
        --kiosk)     KIOSK=1; shift ;;
        --kiosk-user)   KIOSK_USER="${2:?--kiosk-user needs a username}"; shift 2 ;;
        --kiosk-user=*) KIOSK_USER="${1#*=}"; shift ;;
        -h|--help)
            sed -n '3,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

BIN_DIR="$PREFIX/bin"
LIB_DIR="$PREFIX/lib/$APP_ID"
DATA_DIR="$PREFIX/share/$APP_ID"
DESKTOP_DIR="$PREFIX/share/applications"
ICON_ROOT="$PREFIX/share/icons/hicolor"

# Kiosk bits are system-wide wherever the rest went: the unit has to take tty1
# from getty, which is not something a user-level service can do.
KIOSK_CONF_DIR="/etc/synthone"
KIOSK_CONF="$KIOSK_CONF_DIR/kiosk.conf"
KIOSK_UNIT_DIR="/etc/systemd/system"
KIOSK_UNIT="$KIOSK_UNIT_DIR/synthone-kiosk.service"
KIOSK_LAUNCHER="$BIN_DIR/synthone-kiosk"

say()  { printf '  %s\n' "$*"; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

# Anything that can refuse the kiosk is checked here, before a single file is
# written, so a missing sudo does not leave half a setup behind.
if [ "$KIOSK" -eq 1 ] && [ "$ACTION" = install ]; then
    [ "$(id -u)" -eq 0 ] || die "--kiosk needs root (it takes tty1 from getty); re-run with sudo"
    command -v systemctl >/dev/null 2>&1 || die "--kiosk needs systemd"
    [ -d "$HERE/kiosk" ] || die "missing $HERE/kiosk"

    # Who runs it. Root would work but should not: the synth wants a normal
    # user with an audio group, and X refuses to be helpful about it either.
    if [ -z "$KIOSK_USER" ]; then
        KIOSK_USER="${SUDO_USER:-}"
        [ -n "$KIOSK_USER" ] || die "cannot tell which user to run as; pass --kiosk-user NAME"
    fi
    id "$KIOSK_USER" >/dev/null 2>&1 || die "no such user: $KIOSK_USER"
    [ "$(id -u "$KIOSK_USER")" -ne 0 ] || die "refusing to run the kiosk as root; pass --kiosk-user NAME"
    KIOSK_GROUP="$(id -gn "$KIOSK_USER")"
fi

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

    # Kiosk first: stop the service before its binaries go away. Leave
    # kiosk.conf behind, like the presets, so a reinstall keeps the settings.
    if [ -f "$KIOSK_UNIT" ]; then
        if [ "$(id -u)" -ne 0 ]; then
            say "warning: $KIOSK_UNIT needs root to remove; skipping the kiosk"
        else
            systemctl disable --now synthone-kiosk.service >/dev/null 2>&1 || true
            rm -f "$KIOSK_UNIT" && say "removed $KIOSK_UNIT"
            systemctl daemon-reload || true
            systemctl reset-failed synthone-kiosk.service >/dev/null 2>&1 || true
            # Hand tty1 back to getty.
            systemctl enable --now getty@tty1.service >/dev/null 2>&1 || true
            say "tty1 returned to getty"
            say "kept $KIOSK_CONF"
        fi
    fi
    rm -f "$KIOSK_LAUNCHER" 2>/dev/null && say "removed $KIOSK_LAUNCHER" || true

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

# -- kiosk ------------------------------------------------------------------
#
# A bare X server on vt1 with synthone-gui as its only client. Everything is
# generated from the templates in kiosk/ so the paths match this install.

if [ "$KIOSK" -eq 1 ]; then
    echo
    echo "Setting up the kiosk service"

    # Preconditions and $KIOSK_USER / $KIOSK_GROUP were settled up front.
    say "user: $KIOSK_USER ($KIOSK_GROUP)"

    # Groups the kiosk actually needs: audio for the card, video for the GPU,
    # input for the touchscreen, tty for the console X takes over.
    missing_groups=""
    for g in audio video input tty; do
        if getent group "$g" >/dev/null 2>&1 && ! id -nG "$KIOSK_USER" | tr ' ' '\n' | grep -qx "$g"; then
            missing_groups="$missing_groups $g"
        fi
    done
    if [ -n "$missing_groups" ]; then
        for g in $missing_groups; do usermod -aG "$g" "$KIOSK_USER"; done
        say "added $KIOSK_USER to:$missing_groups (takes effect at next login)"
    fi

    # Debian and Raspberry Pi OS wrap Xorg and, by default, only let a user on
    # a local console start it. A service is not a console login, so widen it.
    if [ -f /etc/X11/Xwrapper.config ]; then
        if ! grep -q '^allowed_users=anybody' /etc/X11/Xwrapper.config; then
            say "note: /etc/X11/Xwrapper.config exists and does not say allowed_users=anybody."
            say "      If X refuses to start, add these two lines to it:"
            say "          allowed_users=anybody"
            say "          needs_root_rights=yes"
        fi
    elif [ -d /etc/X11 ]; then
        printf 'allowed_users=anybody\nneeds_root_rights=yes\n' > /etc/X11/Xwrapper.config
        say "wrote /etc/X11/Xwrapper.config"
    fi

    command -v xinit >/dev/null 2>&1 || \
        say "warning: xinit is not installed; the service will fail until you run: apt install xserver-xorg xinit"

    # -- launcher, with this install's paths baked in -----------------------
    sed -e "s|@BIN_DIR@|$BIN_DIR|g" \
        -e "s|@CONFIG@|$KIOSK_CONF|g" \
        "$HERE/kiosk/synthone-kiosk" > "$KIOSK_LAUNCHER"
    chmod 755 "$KIOSK_LAUNCHER"
    say "launcher -> $KIOSK_LAUNCHER"

    # -- config, never clobbered --------------------------------------------
    install -d "$KIOSK_CONF_DIR"
    if [ -f "$KIOSK_CONF" ]; then
        say "kept the existing $KIOSK_CONF"
    else
        install -m 644 "$HERE/kiosk/kiosk.conf" "$KIOSK_CONF"
        say "config -> $KIOSK_CONF"
    fi

    # -- unit ----------------------------------------------------------------
    sed -e "s|@BIN_DIR@|$BIN_DIR|g" \
        -e "s|@KIOSK_CONF@|$KIOSK_CONF|g" \
        -e "s|@KIOSK_USER@|$KIOSK_USER|g" \
        -e "s|@KIOSK_GROUP@|$KIOSK_GROUP|g" \
        "$HERE/kiosk/synthone-kiosk.service" > "$KIOSK_UNIT"
    chmod 644 "$KIOSK_UNIT"
    say "unit -> $KIOSK_UNIT"

    systemctl daemon-reload
    systemctl enable synthone-kiosk.service >/dev/null 2>&1
    say "enabled at boot (tty1 will be taken from getty)"

    echo
    echo "  Kiosk installed but not started, so you do not lose this console."
    echo "  Start it now with:   systemctl start synthone-kiosk"
    echo "  Watch it with:       journalctl -u synthone-kiosk -f"
    echo "  Settings:            $KIOSK_CONF"
    echo "  Turn it off with:    systemctl disable --now synthone-kiosk"
fi

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
