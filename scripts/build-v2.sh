#!/usr/bin/env bash
# GNSS Live Tracker V2: build only; never flashes or erases a connected device.
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
command -v idf.py >/dev/null || { echo 'Activate ESP-IDF v5.4 first; see docs/BUILD.md.' >&2; exit 1; }
if [ "$#" -eq 0 ]; then set -- build; fi
exec idf.py -C "$project_root" "$@"
