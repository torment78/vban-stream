#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
[[ "$(uname -s)" == Darwin ]] || { echo "Run this script on macOS with Xcode." >&2; exit 1; }
cmake --preset macos-universal
cmake --build --preset macos-release --parallel 3
# Qt's headless platform plugin comes from the pinned OBS dependency package.
export QT_PLUGIN_PATH="$PWD/.deps/obs-deps-qt6-2025-07-11-universal/plugins"
ctest --preset macos-release --timeout 120
python3 tools/package-macos.py
