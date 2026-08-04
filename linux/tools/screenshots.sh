#!/usr/bin/env bash
#
# screenshots.sh
# AudioKit Synth One - Linux port
#
# Captures one still per panel at the Raspberry Pi 7" display's resolution and
# assembles them into the slideshow the README shows.
#
#   ./screenshots.sh                        # -> screenshots/{panel}.png + panels.gif
#   ./screenshots.sh --geometry 1024x600    # a different panel
#   ./screenshots.sh --build-dir ../build   # binaries built elsewhere
#   ./screenshots.sh --no-gif               # stills only
#
# The stills are gitignored; panels.gif is the tracked artefact.
#
# Everything runs on a throwaway X server, so this needs no display of its own
# and produces a frame the size of the target panel exactly -- no window
# decoration, no cropping, and the same bare-X arrangement the kiosk uses.
#
# Needs: xvfb-run (xvfb), ImageMagick, and a built synthone-gui.
#

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LINUX="$(cd -- "$HERE/.." && pwd)"
REPO="$(cd -- "$LINUX/.." && pwd)"

BUILD="$LINUX/build"
OUTDIR="$REPO/screenshots"
GEOMETRY="800x480"
MAKE_GIF=1
FRAME_DELAY=200          # centiseconds, so 2s a panel
SETTLE=8                 # seconds to let banks load and the first frames draw

# Panel ring order, matching ChildPanel on iOS.
PANELS=(MAIN ENV PAD FX SEQ TUNE)

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir)   BUILD="${2:?--build-dir needs a directory}"; shift 2 ;;
        --build-dir=*) BUILD="${1#*=}"; shift ;;
        --out)         OUTDIR="${2:?--out needs a directory}"; shift 2 ;;
        --out=*)       OUTDIR="${1#*=}"; shift ;;
        --geometry)    GEOMETRY="${2:?--geometry needs WxH}"; shift 2 ;;
        --geometry=*)  GEOMETRY="${1#*=}"; shift ;;
        --settle)      SETTLE="${2:?--settle needs seconds}"; shift 2 ;;
        --no-gif)      MAKE_GIF=0; shift ;;
        -h|--help)
            sed -n '3,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

say() { printf '  %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

command -v xvfb-run >/dev/null 2>&1 || die "xvfb-run is required (apt install xvfb)"
command -v magick   >/dev/null 2>&1 || die "ImageMagick is required"
GUI="$BUILD/synthone-gui"
[ -x "$GUI" ] || die "no synthone-gui at $GUI (build first, or pass --build-dir)"

case "$GEOMETRY" in
    *x*) ;;
    *)   die "--geometry wants WxH, e.g. 800x480" ;;
esac

install -d "$OUTDIR"
echo "Capturing $GEOMETRY from $GUI"

# ---------------------------------------------------------------------------
# One panel, on its own X server
# ---------------------------------------------------------------------------
#
# GLFW 3.4 selects Wayland whenever WAYLAND_DISPLAY is set and does not fall
# back if that connection fails; libwayland then finds the session's socket
# through XDG_RUNTIME_DIR even when the variable is unset. Hiding both is what
# makes it choose X11 and land on the Xvfb server rather than the desktop.
capture() {
    local panel="$1" out="$2"
    local runtime; runtime="$(mktemp -d)"

    env -u WAYLAND_DISPLAY \
        XDG_RUNTIME_DIR="$runtime" \
        LIBGL_ALWAYS_SOFTWARE=1 \
        LC_ALL=C \
        xvfb-run -a -s "-screen 0 ${GEOMETRY}x24" \
        bash -c '
            set -eu
            "$1" --backend portaudio --fullscreen --top "$2" >/dev/null 2>&1 &
            gui=$!
            sleep "$4"
            kill -0 "$gui" 2>/dev/null || { echo "gui exited early" >&2; exit 1; }
            import -window root "$3"
            kill "$gui" 2>/dev/null || true
            wait "$gui" 2>/dev/null || true
        ' _ "$GUI" "$panel" "$out" "$SETTLE"

    rm -rf "$runtime"
}

for panel in "${PANELS[@]}"; do
    lower="$(printf '%s' "$panel" | tr 'A-Z' 'a-z')"
    out="$OUTDIR/$lower.png"
    capture "$panel" "$out" || die "capture failed for $panel"

    # A capture that races the first frame comes back uniformly black, and the
    # only sign is the mean pixel value -- the file is a perfectly valid PNG.
    mean="$(magick "$out" -format '%[fx:mean*255]' info:)"
    if [ "${mean%%.*}" -lt 5 ]; then
        die "$lower.png looks blank (mean $mean); try --settle $((SETTLE + 4))"
    fi
    say "$lower.png  $(magick identify -format '%wx%h' "$out")  mean $mean"
done

# ---------------------------------------------------------------------------
# Slideshow
# ---------------------------------------------------------------------------
#
# Full self-contained frames on purpose. The smaller inter-frame optimisation
# emits partial frames with dispose=Previous, which ImageMagick renders
# correctly but browsers do not agree on, and it saves almost nothing here
# because flat UI panels already compress well.

if [ "$MAKE_GIF" -eq 1 ]; then
    frames=()
    for panel in "${PANELS[@]}"; do
        frames+=("$OUTDIR/$(printf '%s' "$panel" | tr 'A-Z' 'a-z').png")
    done

    magick -delay "$FRAME_DELAY" -loop 0 "${frames[@]}" \
           -dispose Background -layers OptimizeTransparency +remap -colors 128 \
           "$OUTDIR/panels.gif"
    # Count frames from the whole file: identify on panels.gif[0] selects one
    # frame and dutifully reports "1".
    say "panels.gif  $(magick identify "$OUTDIR/panels.gif" | wc -l) frames, $(magick identify -format '%wx%h' "$OUTDIR/panels.gif[0]"), $(du -h "$OUTDIR/panels.gif" | cut -f1)"
fi

echo
echo "Done. $OUTDIR"
