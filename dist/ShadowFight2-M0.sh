#!/bin/bash
# Shadow Fight 2 - M0 (diagnostico). Nao inicia o jogo: so testa carregar os .so e gera um log.
PORTDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
GAMEDIR="$PORTDIR/sf2"
cd "$GAMEDIR" || exit 1
mkdir -p logs
LOG="$GAMEDIR/logs/m0-$(date +%Y%m%d-%H%M%S).log"
OUT="$PORTDIR/sf2_m0_log.txt"      # copia facil de achar, na mesma pasta do launcher

{
  echo "launcher M0 - $(date)"
  echo "GAMEDIR=$GAMEDIR"
  ls -la "$GAMEDIR/libs" 2>&1
  chmod +x "$GAMEDIR/sf2loader" 2>/dev/null
  unset LD_PRELOAD
  "$GAMEDIR/sf2loader" --m0 "$GAMEDIR/libs"
  echo "codigo de saida: $?"
} > "$LOG" 2>&1

cp -f "$LOG" "$OUT" 2>/dev/null
sync
sleep 2
