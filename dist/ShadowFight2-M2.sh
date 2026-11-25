#!/bin/bash
# ShadowFight2-M2.sh -- Launcher M2: janela SDL2 + render Unity
# Coloque em ports/ no cartao SD do R36 Ultra X

PORTDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
GAMEDIR="$PORTDIR/sf2"
APK="$GAMEDIR/game.apk"
SAVEDIR="$GAMEDIR/save"
LOGDIR="$GAMEDIR/logs"
LOG="$LOGDIR/m2-$(date +%Y%m%d-%H%M%S).log"

mkdir -p "$LOGDIR" "$SAVEDIR"

if [ ! -f "$APK" ]; then
    echo "ERRO: APK nao encontrado em $APK" | tee "$PORTDIR/sf2_m2_log.txt"
    exit 1
fi

export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"
chmod +x "$GAMEDIR/sf2loader"
unset LD_PRELOAD

"$GAMEDIR/sf2loader" --m2 "$GAMEDIR/libs" "$APK" "$SAVEDIR" > "$LOG" 2>&1

cp -f "$LOG" "$PORTDIR/sf2_m2_log.txt"
sync
