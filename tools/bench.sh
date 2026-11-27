#!/usr/bin/env bash
# Mesure du cout CPU de WallEnginePaper sur quelques fonds types (ligne "Session :" du journal).
# Usage : tools/bench.sh [secondes] [fond...]   (depuis la racine du depot, instance quittee avant)
set -u
EXE="build/vs2026/Release/WallEnginePaper.exe"
LOG="$APPDATA/WallEnginePaper/log.txt"
SECS="${1:-30}"; shift || true
TARGETS=("$@")
if [ ${#TARGETS[@]} -eq 0 ]; then
  TARGETS=("assets/shaders/aurora.hlsl" "res/logo.png")
fi
"$EXE" --quit >/dev/null 2>&1; sleep 2
for t in "${TARGETS[@]}"; do
  "$EXE" --minimized --no-autopause --exit-after "$SECS" --wallpaper "$t"
  line=$(grep "Session :" "$LOG" | tail -1 | sed 's/.*Session : //')
  echo "$(basename "$t") | $line"
done
