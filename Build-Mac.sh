#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
[[ "$(uname -s)" == Darwin ]] || { echo "Run this script on macOS with Xcode." >&2; exit 1; }
cmake --preset macos-universal
cmake --build --preset macos-release --parallel 3
# Qt's headless platform plugin comes from the pinned OBS dependency package.
qt_root="$PWD/.deps/obs-deps-qt6-2025-07-11-universal"
qt_minimal="$(find "$qt_root" -name libqminimal.dylib -print -quit)"
[[ -n "$qt_minimal" ]] || { echo "Qt minimal test platform not found" >&2; exit 1; }
export QT_PLUGIN_PATH="$(dirname "$(dirname "$qt_minimal")")"
export DYLD_FRAMEWORK_PATH="$PWD/.deps/Frameworks:$qt_root/lib"
export DYLD_LIBRARY_PATH="$PWD/.deps/lib:$PWD/.deps/obs-deps-2025-07-11-universal/lib"
ctest --preset macos-release --timeout 120
python3 tools/package-macos.py
