#!/bin/bash
# ShadowFight2-M2.sh -- Launcher M2: janela SDL2 + render Unity
# Coloque em ports/ no cartao SD do R36 Ultra X

PORTDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
GAMEDIR="$PORTDIR/sf2"
APK="$GAMEDIR/game.apk"
SAVEDIR="$GAMEDIR/save"
LOGDIR="$GAMEDIR/logs"
LOG="$LOGDIR/m2-$(date +%Y%m%d-%H%M%S).log"

# Limites de seguranca: o log nunca passa de 5 MB e o loader e morto
# apos SF2_TIMEOUT segundos (padrao 900 = 15 min). Use SF2_TIMEOUT=0 para
# desativar o limite, ou SF2_TIMEOUT=60 para testes curtos.
# Obs.: o jogo ainda nao tem entrada pelo controle (M3); ESC fecha a janela.
MAX_LOG_BYTES=5000000
MAX_SECONDS="${SF2_TIMEOUT:-900}"

mkdir -p "$LOGDIR" "$SAVEDIR"

# Mantem so os 5 logs mais recentes (evita encher o cartao SD)
ls -1t "$LOGDIR"/m2-*.log 2>/dev/null | tail -n +6 | xargs -r rm -f
rm -f "$SAVEDIR"/tombstone_* 2>/dev/null

if [ ! -f "$APK" ]; then
    echo "ERRO: APK nao encontrado em $APK" | tee "$PORTDIR/sf2_m2_log.txt"
    exit 1
fi

# Runtime dir (silencia o erro do pulse: /run/user/1000/pulse)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/runtime-$(id -u)}"
mkdir -p "$XDG_RUNTIME_DIR" 2>/dev/null
chmod 700 "$XDG_RUNTIME_DIR" 2>/dev/null

export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"
chmod +x "$GAMEDIR/sf2loader"
unset LD_PRELOAD

# timeout -k: SIGTERM e, se nao sair em 5 s, SIGKILL.
# head -c: corta o log; ao fechar o pipe o loader recebe SIGPIPE e morre.
timeout -k 5 "$MAX_SECONDS" "$GAMEDIR/sf2loader" --m2 "$GAMEDIR/libs" "$APK" "$SAVEDIR" 2>&1 \
    | head -c "$MAX_LOG_BYTES" > "$LOG"
RC=${PIPESTATUS[0]}

{
    echo
    echo "=== launcher: sf2loader terminou com codigo $RC (124/137 = limite de tempo SF2_TIMEOUT, 3 = Unity kill(), 139 = SIGSEGV) ==="
} >> "$LOG"

cp -f "$LOG" "$PORTDIR/sf2_m2_log.txt"
# Se houve tombstone, anexa ao log copiado
for t in "$SAVEDIR"/tombstone_*; do
    [ -f "$t" ] && { echo; echo "=== $t ==="; head -c 20000 "$t"; } >> "$PORTDIR/sf2_m2_log.txt"
done
sync
