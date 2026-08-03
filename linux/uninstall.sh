#!/usr/bin/env bash
#
# uninstall.sh
# AudioKit Synth One - Linux port
#
# Convenience wrapper; the logic lives in install.sh so there is one copy of it.
#
#   ./uninstall.sh                    # remove from ~/.local (or /usr/local as root)
#   ./uninstall.sh --prefix /opt/foo  # remove from elsewhere
#
# Presets you saved are never touched.
#

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "$HERE/install.sh" --uninstall "$@"
