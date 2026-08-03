#!/usr/bin/env bash
#
# build-arm64.sh
# AudioKit Synth One - Linux port
#
# Builds the aarch64 binaries inside an arm64 container. Useful from an x86_64
# workstation; on real ARM64 hardware just use the normal cmake commands.
#
# Needs Docker with qemu binfmt handlers registered for arm64:
#
#     docker run --privileged --rm tonistiigi/binfmt --install arm64
#
# Emulated compilation is slow -- expect several minutes.
#
#   ./build-arm64.sh                  # -> build-arm64/
#   ./build-arm64.sh --package        # also writes dist/synthone-linux-aarch64.zip
#

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd -- "$HERE/.." && pwd)"
OUT="$HERE/build-arm64"
IMAGE="docker.io/library/ubuntu:24.04"
DO_PACKAGE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --package)  DO_PACKAGE=1; shift ;;
        --outdir)   OUT="${2:?--outdir needs a directory}"; shift 2 ;;
        -h|--help)  sed -n '3,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

command -v docker >/dev/null 2>&1 || { echo "error: docker is required" >&2; exit 1; }
if ! ls /proc/sys/fs/binfmt_misc/ 2>/dev/null | grep -qi aarch64; then
    echo "error: no arm64 binfmt handler. Register one with:" >&2
    echo "    docker run --privileged --rm tonistiigi/binfmt --install arm64" >&2
    exit 1
fi

mkdir -p "$OUT"

# The source is mounted read-only; everything written goes to $OUT. Running as
# the invoking user keeps the artifacts from coming out root-owned.
docker run --rm --platform linux/arm64 \
    -v "$REPO:/src:ro" \
    -v "$OUT:/out" \
    -e DEBIAN_FRONTEND=noninteractive \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    "$IMAGE" bash -euo pipefail -c '
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends \
            clang cmake ninja-build git make pkg-config ca-certificates binutils \
            libasound2-dev libjack-jackd2-dev portaudio19-dev \
            libglfw3-dev libgl-dev >/dev/null

        echo "--- toolchain: $(clang --version | head -1) on $(uname -m)"

        cmake -S /src/linux -B /out/cmake -G Ninja \
              -DCMAKE_BUILD_TYPE=Release >/dev/null
        cmake --build /out/cmake

        mkdir -p /out/bin
        cp /out/cmake/synthone /out/cmake/synthone-gui /out/cmake/synthone-offline /out/bin/

        # Strip here, where a native aarch64 strip exists; the host one cannot
        # handle a foreign architecture, so package.sh skips it.
        strip --strip-unneeded /out/bin/* || true

        echo "--- built:"
        ls -la /out/bin | awk 'NR>1 {printf "    %-20s %s bytes\n", $NF, $5}'

        # Docker runs as root; hand the artifacts back to the invoking user so
        # the build directory stays manageable on the host.
        chown -R "${HOST_UID}:${HOST_GID}" /out
    '

echo
echo "aarch64 binaries in $OUT/bin"

if [ "$DO_PACKAGE" = 1 ]; then
    "$HERE/package.sh" --build-dir "$OUT/bin" --arch aarch64
fi
