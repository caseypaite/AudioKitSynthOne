#!/usr/bin/env bash
#
# package.sh
# AudioKit Synth One - Linux port
#
# Assembles a self-contained distribution archive: the three binaries, the
# runtime data they need, the icon, and the install/uninstall scripts.
#
#   ./package.sh                 # -> dist/synthone-linux-x86_64.zip
#   ./package.sh --outdir /tmp   # write the archive elsewhere
#
# The archive is standalone: install.sh detects the packaged layout and does
# not need the source checkout.
#

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd -- "$HERE/.." && pwd)"
RESOURCES="$REPO/AudioKitSynthOne"
BUILD="$HERE/build"

APP_ID="synthone"
ARCH="$(uname -m)"
OUTDIR="$HERE/dist"
BIN_SRC="$BUILD"          # where the binaries come from
ARCH_OVERRIDE=""          # label the archive as this arch instead of the host's
BINARIES=(synthone-gui synthone synthone-offline)

while [ $# -gt 0 ]; do
    case "$1" in
        --outdir)   OUTDIR="${2:?--outdir needs a directory}"; shift 2 ;;
        --build-dir) BIN_SRC="${2:?--build-dir needs a directory}"; shift 2 ;;
        --arch)      ARCH_OVERRIDE="${2:?--arch needs a name}"; shift 2 ;;
        --outdir=*) OUTDIR="${1#*=}"; shift ;;
        -h|--help)  sed -n '3,15p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

[ -n "$ARCH_OVERRIDE" ] && ARCH="$ARCH_OVERRIDE"
NAME="$APP_ID-linux-$ARCH"

say() { printf '  %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

command -v zip >/dev/null 2>&1 || die "zip is required (pacman -S zip)"

echo "Packaging $NAME"

# -- build ------------------------------------------------------------------

if [ "$BIN_SRC" = "$BUILD" ] && [ ! -x "$BUILD/synthone-gui" ]; then
    say "building first"
    cmake -S "$HERE" -B "$BUILD" -G Ninja >/dev/null
    cmake --build "$BUILD" >/dev/null
fi
for b in "${BINARIES[@]}"; do
    [ -x "$BIN_SRC/$b" ] || die "missing $BIN_SRC/$b"
done

# -- assemble ---------------------------------------------------------------

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
ROOT="$STAGE/$NAME"

install -d "$ROOT/bin" "$ROOT/share/$APP_ID/DSP" "$ROOT/share/$APP_ID/Presets"

for b in "${BINARIES[@]}"; do
    install -m 755 "$BIN_SRC/$b" "$ROOT/bin/$b"
done
# Strip debug info; the binaries carry -g from the Release flags.
# Host `strip` cannot handle a foreign architecture; skip it when cross-packaging.
if [ -z "$ARCH_OVERRIDE" ] && command -v strip >/dev/null 2>&1; then
    strip --strip-unneeded "$ROOT"/bin/* 2>/dev/null || true
fi
say "binaries"

cp -r "$RESOURCES/DSP/BandlimitedWavetables" "$ROOT/share/$APP_ID/DSP/"
cp -r "$RESOURCES/Presets/Data" "$ROOT/share/$APP_ID/Presets/"
[ -f "$HERE/data/tunings.json" ] \
    && install -m 644 "$HERE/data/tunings.json" "$ROOT/share/$APP_ID/tunings.json" \
    || say "warning: data/tunings.json missing"
say "runtime data"

ICON_SRC="$RESOURCES/Assets/Assets.xcassets/AppIcon.appiconset/Icon 512.png"
[ -f "$ICON_SRC" ] && install -m 644 "$ICON_SRC" "$ROOT/share/icon.png" \
    || say "warning: icon missing"

install -m 755 "$HERE/install.sh"   "$ROOT/install.sh"
install -m 755 "$HERE/uninstall.sh" "$ROOT/uninstall.sh"
[ -f "$REPO/LICENSE" ] && install -m 644 "$REPO/LICENSE" "$ROOT/LICENSE" || true
say "scripts"

# The Raspberry Pi kiosk templates. install.sh --kiosk refuses to run without
# them, so they are not optional: a missing one is a packaging bug, not a
# degraded archive.
install -d "$ROOT/kiosk"
for f in synthone-kiosk synthone-kiosk.service kiosk.conf; do
    [ -f "$HERE/kiosk/$f" ] || die "missing $HERE/kiosk/$f"
    install -m 644 "$HERE/kiosk/$f" "$ROOT/kiosk/$f"
done
say "kiosk templates"

# -- readme -----------------------------------------------------------------

cat > "$ROOT/README.txt" <<'README'
AudioKit Synth One - Linux port
===============================

A native Linux build of the Synth One synthesis engine, with a graphical front
end, a headless JACK/PortAudio host, and an offline renderer.

The DSP is the original Synth One kernel; the interface is a rebuild (Dear
ImGui), not the iOS UI.


Install
-------

    ./install.sh                      installs to ~/.local
    sudo ./install.sh --prefix /usr/local     system-wide

Adds "AudioKit Synth One" to your application menu, and puts synthone-gui,
synthone and synthone-offline on your PATH.

    ./uninstall.sh                    removes it again (keeps your presets)


Requirements
------------

@ARCH@ Linux with a reasonably current glibc. These shared libraries must be
present:

    alsa-lib          MIDI input (required)
    glfw, libGL       the graphical front end
    jack              JACK output   - pipewire-jack also works
    portaudio         alternative audio backend
    libX11 / xcb      windowing

On Arch:

    sudo pacman -S --needed alsa-lib glfw pipewire-jack portaudio

On Debian/Ubuntu:

    sudo apt install libasound2 libglfw3 libjack-jackd2-0 libportaudio2

If a backend's library is missing the binary will not start; build from source
to get a version without it.


Running
-------

    synthone-gui                          graphical, JACK by default
    synthone-gui --backend portaudio
    synthone --backend jack --bank "Starter Bank" --preset 0
    synthone-offline --note 60 --seconds 4 out.wav      no audio hardware needed

MIDI arrives on an ALSA sequencer port; by default it subscribes to every
source it finds. `synthone --list-midi` lists them.


Where things go
---------------

    <prefix>/lib/synthone/         binaries
    <prefix>/share/synthone/       wavetables, factory presets, tuning library
    <prefix>/bin/                  wrappers that point the binaries at the above

Presets you save go to $XDG_DATA_HOME/synthone/presets, defaulting to
~/.local/share/synthone/presets. The factory banks are never modified.


Raspberry Pi kiosk
------------------

    sudo ./install.sh --kiosk
    sudo ./install.sh --kiosk --kiosk-user pi

Adds a systemd service that boots straight into the synth: a bare X server on
vt1 with synthone-gui as its only client, no desktop and no window manager. It
needs root, because it takes tty1 away from getty. Requires xinit:

    sudo apt install xserver-xorg xinit

Installed but not started, so you keep the console you ran it from:

    systemctl start synthone-kiosk
    journalctl -u synthone-kiosk -f
    systemctl disable --now synthone-kiosk

Settings (audio backend, geometry, MIDI source) live in /etc/synthone/kiosk.conf
and survive reinstalls. The templates they are generated from are in kiosk/.
README
sed -i "s/@ARCH@/$ARCH/" "$ROOT/README.txt"
say "readme"

# -- preflight --------------------------------------------------------------
#
# Everything install.sh reads out of the archive, checked here rather than on
# the target machine. The kiosk templates were missing from the archive once.

for p in bin/synthone-gui bin/synthone bin/synthone-offline \
         "share/$APP_ID/DSP/BandlimitedWavetables" "share/$APP_ID/Presets/Data" \
         install.sh uninstall.sh \
         kiosk/synthone-kiosk kiosk/synthone-kiosk.service kiosk/kiosk.conf; do
    [ -e "$ROOT/$p" ] || die "staged tree is missing $p"
done
say "preflight"

# -- zip --------------------------------------------------------------------

install -d "$OUTDIR"
ZIP="$OUTDIR/$NAME.zip"
rm -f "$ZIP"
( cd "$STAGE" && zip -qr "$ZIP" "$NAME" )

# Checksum next to the archive, for release verification.
if command -v sha256sum >/dev/null 2>&1; then
    ( cd "$OUTDIR" && sha256sum "$(basename "$ZIP")" > "$(basename "$ZIP").sha256" )
fi

echo
echo "Done."
echo "  $ZIP  ($(du -h "$ZIP" | cut -f1))"
[ -f "$ZIP.sha256" ] && echo "  $ZIP.sha256"
echo "  unzip it anywhere and run ./install.sh"
