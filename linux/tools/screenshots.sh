#!/usr/bin/env bash
#
# screenshots.sh
# AudioKit Synth One - Linux port
#
# Captures one still per panel at the Raspberry Pi 7" display's resolution and
# assembles them into the slideshow the README shows.
#
#   ./screenshots.sh                        # -> screenshots/{panel}.png + panels.gif
#   ./screenshots.sh --desktop              # -> screenshots/desktop-*.png + desktop.gif
#   ./screenshots.sh --geometry 1024x600    # a different panel
#   ./screenshots.sh --build-dir ../build   # binaries built elsewhere
#   ./screenshots.sh --no-gif               # stills only
#
# The two modes match the two layouts: a small display shows one panel, a
# desktop shows two stacked, so the desktop run shoots pairs rather than
# singles and three frames cover all six panels.
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
DESKTOP=0
MAKE_GIF=1
FRAME_DELAY=200          # centiseconds, so 2s a panel
SETTLE=8                 # seconds to let banks load and the first frames draw

# Panel ring order, matching ChildPanel on iOS.
PANELS=(MAIN ENV PAD FX SEQ TUNE)

# Desktop pairs: every panel once, each beside one it is actually used with.
PAIRS=("MAIN FX" "ENV SEQ" "PAD TUNE")

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir)   BUILD="${2:?--build-dir needs a directory}"; shift 2 ;;
        --build-dir=*) BUILD="${1#*=}"; shift ;;
        --out)         OUTDIR="${2:?--out needs a directory}"; shift 2 ;;
        --out=*)       OUTDIR="${1#*=}"; shift ;;
        --geometry)    GEOMETRY="${2:?--geometry needs WxH}"; shift 2 ;;
        --geometry=*)  GEOMETRY="${1#*=}"; shift ;;
        --settle)      SETTLE="${2:?--settle needs seconds}"; shift 2 ;;
        --desktop)     DESKTOP=1; shift ;;
        --no-gif)      MAKE_GIF=0; shift ;;
        -h|--help)
            sed -n '3,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

# --desktop only changes the defaults; an explicit --geometry still wins.
if [ "$DESKTOP" -eq 1 ] && [ "$GEOMETRY" = "800x480" ]; then
    GEOMETRY="1440x900"
fi

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
    local out="$1"; shift          # remaining arguments go to synthone-gui
    local runtime; runtime="$(mktemp -d)"

    env -u WAYLAND_DISPLAY \
        XDG_RUNTIME_DIR="$runtime" \
        LIBGL_ALWAYS_SOFTWARE=1 \
        LC_ALL=C \
        xvfb-run -a -s "-screen 0 ${GEOMETRY}x24" \
        bash -c '
            set -eu
            gui_bin="$1"; out="$2"; settle="$3"; shift 3
            "$gui_bin" --backend portaudio --fullscreen "$@" >/dev/null 2>&1 &
            gui=$!
            sleep "$settle"
            kill -0 "$gui" 2>/dev/null || { echo "gui exited early" >&2; exit 1; }
            import -window root "$out"
            kill "$gui" 2>/dev/null || true
            wait "$gui" 2>/dev/null || true
        ' _ "$GUI" "$out" "$SETTLE" "$@"

    rm -rf "$runtime"
}

# Fails loudly on a capture that raced the first frame: it comes back uniformly
# black, and the file is a perfectly valid PNG either way.
check_not_blank() {
    local f="$1" mean
    mean="$(magick "$f" -format '%[fx:mean*255]' info:)"
    if [ "${mean%%.*}" -lt 5 ]; then
        die "$(basename "$f") looks blank (mean $mean); try --settle $((SETTLE + 4))"
    fi
    printf '%s' "$mean"
}

SHOTS=()

if [ "$DESKTOP" -eq 1 ]; then
    for pair in "${PAIRS[@]}"; do
        top="${pair%% *}"; bottom="${pair##* }"
        name="desktop-$(printf '%s-%s' "$top" "$bottom" | tr 'A-Z' 'a-z').png"
        out="$OUTDIR/$name"
        capture "$out" --top "$top" --bottom "$bottom" || die "capture failed for $pair"
        mean="$(check_not_blank "$out")"
        say "$name  $(magick identify -format '%wx%h' "$out")  mean $mean"
        SHOTS+=("$out")
    done
else
    for panel in "${PANELS[@]}"; do
        lower="$(printf '%s' "$panel" | tr 'A-Z' 'a-z')"
        out="$OUTDIR/$lower.png"
        capture "$out" --top "$panel" || die "capture failed for $panel"
        mean="$(check_not_blank "$out")"
        say "$lower.png  $(magick identify -format '%wx%h' "$out")  mean $mean"
        SHOTS+=("$out")
    done
fi

# ---------------------------------------------------------------------------
# Slideshow
# ---------------------------------------------------------------------------
#
# Full self-contained frames on purpose. The smaller inter-frame optimisation
# emits partial frames with dispose=Previous, which ImageMagick renders
# correctly but browsers do not agree on, and it saves almost nothing here
# because flat UI panels already compress well.

if [ "$MAKE_GIF" -eq 1 ]; then
    GIF="$OUTDIR/panels.gif"
    [ "$DESKTOP" -eq 1 ] && GIF="$OUTDIR/desktop.gif"

    magick -delay "$FRAME_DELAY" -loop 0 "${SHOTS[@]}" \
           -dispose Background -layers OptimizeTransparency +remap -colors 128 \
           "$GIF"
    # Count frames from the whole file: identify on file[0] selects one frame
    # and dutifully reports "1".
    say "$(basename "$GIF")  $(magick identify "$GIF" | wc -l) frames, $(magick identify -format '%wx%h' "$GIF[0]"), $(du -h "$GIF" | cut -f1)"
fi

echo
echo "Done. $OUTDIR"
