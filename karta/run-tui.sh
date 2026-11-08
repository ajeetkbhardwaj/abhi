#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$DIR/build/karta"
if [[ ! -x "$BIN" ]]; then
  echo "karta binary not found at $BIN; building..." >&2
  cmake -S "$DIR" -B "$DIR/build" -DKARTA_BUILD_TUI=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build "$DIR/build" -j"${KARTA_JOBS:-4}"
fi
exec "$BIN" "$@"
