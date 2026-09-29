#!/usr/bin/env bash
# Logpuffer des Spielprogramms (debug_log im internen RAM) per ST-LINK
# auslesen und als Text ausgeben. Das Programm läuft dabei weiter.
#
# Aufruf aus firmware/: tools/read-log.sh
set -euo pipefail
cd "$(dirname "$0")/.."
TOOLCHAIN="${TOOLCHAIN:-$HOME/tools/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi/bin}"
read -r ADDR SIZE < <("$TOOLCHAIN/arm-none-eabi-nm" -S build/game.elf | awk '$4 == "debug_log" { print $1, $2 }')
[ -n "${ADDR:-}" ] || { echo "debug_log fehlt in build/game.elf" >&2; exit 1; }
DUMP=$(mktemp)
trap 'rm -f "$DUMP"' EXIT
openocd -f board/stm32f429discovery.cfg -c "init" \
  -c "dump_image $DUMP 0x$ADDR $((16#$SIZE))" -c "exit" >/dev/null 2>&1
python3 - "$DUMP" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
magic, head = struct.unpack_from("<II", data)
text = data[8:]
if magic != 0x474F4C44:
    sys.exit("Kein Log (Magic %08x) – läuft das Spielprogramm?" % magic)
n = len(text)
out = text[:head] if head <= n else text[head % n:] + text[:head % n]
sys.stdout.write(out.decode("utf-8", "replace"))
PY
