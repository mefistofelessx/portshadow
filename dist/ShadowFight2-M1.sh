#!/bin/bash
# Shadow Fight 2 - M1 (shims Bionic, thunks softfp, .init_array e JNI_OnLoad).
PORTDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
GAMEDIR="$PORTDIR/sf2"
cd "$GAMEDIR" || exit 1
mkdir -p logs
LOG="$GAMEDIR/logs/m1-$(date +%Y%m%d-%H%M%S).log"
OUT="$PORTDIR/sf2_m1_log.txt"

{
  echo "launcher M1 - $(date)"
  echo "GAMEDIR=$GAMEDIR"
  ls -la "$GAMEDIR/libs" 2>&1
  chmod +x "$GAMEDIR/sf2loader" 2>/dev/null
  unset LD_PRELOAD
  "$GAMEDIR/sf2loader" --m1 "$GAMEDIR/libs"
  echo "codigo de saida: $?"
} > "$LOG" 2>&1

cp -f "$LOG" "$OUT" 2>/dev/null
sync
sleep 2
