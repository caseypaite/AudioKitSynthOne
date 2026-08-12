#!/usr/bin/env bash
#
# build-windows.sh
# AudioKit Synth One - Windows cross build
#
# Cross-compiles the Windows binaries from Linux with MinGW-w64. There is no
# MSVC path and nothing here needs to run on Windows.
#
#   ./build-windows.sh                 # -> build-windows/
#   ./build-windows.sh --package       # also writes dist/synthone-windows-x86_64.zip
#   ./build-windows.sh --mingw DIR     # toolchain not on PATH
#
# Needs the mingw-w64 cross toolchain:
#
#     sudo apt install mingw-w64          # Debian/Ubuntu
#     sudo pacman -S mingw-w64-gcc        # Arch
#
# or an unpacked toolchain tree pointed at with --mingw / $MINGW_PREFIX.
#
# The result is standalone: everything third-party (Soundpipe, PortAudio, GLFW,
# Dear ImGui) is cross-built and linked statically, as are libstdc++ and
# libwinpthread, so the binaries import nothing but Windows' own DLLs.
#

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd -- "$HERE/.." && pwd)"
RESOURCES="$REPO/AudioKitSynthOne"

TRIPLE="x86_64-w64-mingw32"
BUILD="$HERE/build-windows"
OUTDIR="$HERE/dist"
MINGW="${MINGW_PREFIX:-}"
DO_PACKAGE=0
BINARIES=(synthone-gui.exe synthone.exe synthone-offline.exe latency_test.exe)

while [ $# -gt 0 ]; do
    case "$1" in
        --package)   DO_PACKAGE=1; shift ;;
        --mingw)     MINGW="${2:?--mingw needs a directory}"; shift 2 ;;
        --builddir)  BUILD="${2:?--builddir needs a directory}"; shift 2 ;;
        --outdir)    OUTDIR="${2:?--outdir needs a directory}"; shift 2 ;;
        -h|--help)   sed -n '3,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

say() { printf '  %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# -- toolchain --------------------------------------------------------------

CMAKE_ARGS=(-DCMAKE_TOOLCHAIN_FILE="$HERE/cmake/toolchain-$TRIPLE.cmake")
if [ -n "$MINGW" ]; then
    [ -x "$MINGW/usr/bin/$TRIPLE-gcc" ] || [ -x "$MINGW/bin/$TRIPLE-gcc" ] \
        || die "no $TRIPLE-gcc under $MINGW"
    CMAKE_ARGS+=(-DMINGW_PREFIX="$MINGW")
    export PATH="$MINGW/usr/bin:$MINGW/bin:$PATH"
elif ! command -v "$TRIPLE-gcc" >/dev/null 2>&1; then
    die "$TRIPLE-gcc not found. Install mingw-w64, or pass --mingw DIR."
fi

echo "Building Synth One for Windows ($TRIPLE)"
say "toolchain: $("$TRIPLE-gcc" --version | head -1)"

# -- build ------------------------------------------------------------------

cmake -S "$HERE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release "${CMAKE_ARGS[@]}" >/dev/null
cmake --build "$BUILD"

for b in "${BINARIES[@]}"; do
    [ -f "$BUILD/$b" ] || die "missing $BUILD/$b"
done

# Resources go beside the binaries so the build tree runs in place, the same
# layout the archive uses -- Engine::defaultResourceDir() looks here first.
stage_resources() {
    local dest="$1"
    mkdir -p "$dest/resources/DSP" "$dest/resources/Presets" "$dest/data"
    cp -r "$RESOURCES/DSP/BandlimitedWavetables" "$dest/resources/DSP/"
    cp -r "$RESOURCES/Presets/Data" "$dest/resources/Presets/"
    cp "$HERE/data/tunings.json" "$dest/data/"
}
stage_resources "$BUILD"

echo
say "binaries in $BUILD"
for b in "${BINARIES[@]}"; do
    say "  $b  $(stat -c %s "$BUILD/$b") bytes"
done

[ "$DO_PACKAGE" = 1 ] || exit 0

# -- package ----------------------------------------------------------------

command -v zip >/dev/null 2>&1 || die "zip is required"

NAME="synthone-windows-x86_64"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
DEST="$STAGE/$NAME"
mkdir -p "$DEST"

echo
echo "Packaging $NAME"

for b in "${BINARIES[@]}"; do
    cp "$BUILD/$b" "$DEST/"
    "$TRIPLE-strip" --strip-unneeded "$DEST/$b" 2>/dev/null || true
done
stage_resources "$DEST"

cat > "$DEST/README.txt" <<'EOF'
AudioKit Synth One - Windows build
==================================

Unzip anywhere and run. Nothing to install: the binaries import only Windows'
own DLLs, and they find their presets and wavetables in the resources\ and
data\ folders next to them, so keep the folder together.

  synthone-gui.exe        the synth, with its full interface
  synthone.exe            command-line host; play it from a MIDI keyboard
  synthone-offline.exe    render a preset to a .wav, no audio hardware needed
  latency_test.exe        measure real round-trip audio latency (needs a
                          loopback cable, or a virtual loopback device)

Quick start:

  synthone-gui.exe
  synthone.exe --list                     list the preset banks
  synthone.exe --list-midi                list MIDI input devices
  synthone.exe --bank BankA --preset 0    play, driven by a MIDI keyboard
  synthone.exe --test-note 60             no controller? loop a test note

Audio:  output goes through PortAudio, which wraps several Windows driver
families. WASAPI is used by default; --host-api picks another
(wasapi, directsound, mme, wdmks). If audio stutters, try a larger
--buffer, a longer --latency, or a different --host-api.

MIDI:   input devices are opened through WinMM. --list-midi numbers them and
--midi N selects one; the default opens all of them.

Presets you save go to %APPDATA%\SynthOne\presets. The banks that ship in
resources\ are never written to.
EOF

( cd "$STAGE" && zip -qr "$NAME.zip" "$NAME" )
mkdir -p "$OUTDIR"
mv "$STAGE/$NAME.zip" "$OUTDIR/"

say "wrote $OUTDIR/$NAME.zip  ($(stat -c %s "$OUTDIR/$NAME.zip") bytes)"
