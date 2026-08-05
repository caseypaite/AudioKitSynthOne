#!/usr/bin/env bash
#
# bench-latency.sh
# AudioKit Synth One - Linux port
#
# Measures the audio thread's actual scheduling class and priority after
# synthone starts, then reports:
#   - /proc/<pid>/status sched fields
#   - chrt output for the audio thread
#   - CPU governor (all cores)
#   - cyclictest latency histogram (if cyclictest is installed)
#
# Usage (on the Pi):
#   # Start synthone in one terminal, then in another:
#   ./bench-latency.sh [pid-of-synthone]
#
#   # Or let the script find the pid itself:
#   ./bench-latency.sh
#
# The script is read-only and safe to run against a live session.
#

set -euo pipefail

DURATION=10       # seconds to run cyclictest / collect stats
INTERVAL_US=500   # cyclictest interval (µs) — 500µs = 2kHz probe rate

# ---------------------------------------------------------------------------
# Find the synthone PID
# ---------------------------------------------------------------------------

SYNTH_PID="${1:-}"
if [ -z "$SYNTH_PID" ]; then
    SYNTH_PID=$(pgrep -x synthone-gui 2>/dev/null || pgrep -x synthone 2>/dev/null || true)
    if [ -z "$SYNTH_PID" ]; then
        echo "error: synthone is not running. Start it first, then run this script."
        echo "       Or pass the pid: $0 <pid>"
        exit 1
    fi
    echo "Found synthone PID: $SYNTH_PID"
fi

# ---------------------------------------------------------------------------
# Scheduling class and priority of the audio thread
# ---------------------------------------------------------------------------

echo
echo "=== Thread scheduling (PID $SYNTH_PID) ==="

# List all threads; the audio callback thread is typically the one with
# the highest RT priority. We report all of them so the caller can spot it.
if command -v chrt >/dev/null 2>&1; then
    echo "chrt per-thread:"
    for tid in /proc/"$SYNTH_PID"/task/*/; do
        tid_num="$(basename "$tid")"
        sched="$(chrt -p "$tid_num" 2>/dev/null || echo "  (chrt failed for tid $tid_num)")"
        comm="$(cat "$tid/comm" 2>/dev/null || echo "?")"
        printf "  tid %-6s  %-20s  %s\n" "$tid_num" "$comm" "$sched"
    done
else
    echo "  chrt not installed; install util-linux for scheduling details"
fi

echo
echo "=== /proc/$SYNTH_PID/status (sched fields) ==="
grep -E '^(Name|voluntary|nonvoluntary|Threads)' /proc/"$SYNTH_PID"/status 2>/dev/null || true

# ---------------------------------------------------------------------------
# CPU governor
# ---------------------------------------------------------------------------

echo
echo "=== CPU governor ==="
for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    cpu="$(echo "$f" | grep -o 'cpu[0-9]*')"
    gov="$(cat "$f" 2>/dev/null || echo unavailable)"
    freq="$(cat "$(dirname "$f")/scaling_cur_freq" 2>/dev/null || echo '?')"
    printf "  %-6s  %-14s  %s kHz\n" "$cpu" "$gov" "$freq"
done

# ---------------------------------------------------------------------------
# cyclictest — scheduling latency histogram
# ---------------------------------------------------------------------------

echo
if ! command -v cyclictest >/dev/null 2>&1; then
    echo "=== cyclictest ==="
    echo "  Not installed. For latency numbers: sudo apt install rt-tests"
    echo "  Then re-run this script while the synth is running."
    exit 0
fi

echo "=== cyclictest (${DURATION}s, interval ${INTERVAL_US}µs) ==="
echo "  Running as $(id -un). Results will be skewed if not RT-capable."
echo

# -m  = lock memory
# -p  = priority (run at same level as the audio thread to get comparable numbers)
# -i  = interval in µs
# -D  = duration
# -h  = histogram (max latency bucket in µs)
# -q  = quiet (only print the histogram)
sudo cyclictest \
    -m \
    -p 95 \
    -i "$INTERVAL_US" \
    -D "${DURATION}s" \
    -h 200 \
    -q \
    2>&1 || {
        echo "  cyclictest failed (try running as root: sudo $0)"
    }
