#!/usr/bin/env bash
# Letzten Prozessorfehler (board_fault) aus dem Board lesen und aufschlüsseln.
# Aufruf aus firmware/: tools/read-fault.sh [elf]   (Standard: build/game.elf)
set -euo pipefail
cd "$(dirname "$0")/.."
ELF="${1:-build/game.elf}"
TOOLCHAIN="${TOOLCHAIN:-$HOME/tools/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi/bin}"
ADDR=$("$TOOLCHAIN/arm-none-eabi-nm" "$ELF" | awk '$3 == "board_fault" { print $1 }')
[ -n "$ADDR" ] || { echo "board_fault fehlt in $ELF" >&2; exit 1; }
WORDS=$(openocd -f board/stm32f429discovery.cfg -c init -c "mdw 0x$ADDR 16" -c exit 2>&1 | grep '^0x' | cut -d: -f2)
python3 - "$ELF" "$TOOLCHAIN" $WORDS <<'PY'
import subprocess, sys
elf, tc, *w = sys.argv[1:]
w = [int(x, 16) for x in w]
if w[0] != 0x544C5546:
    sys.exit("Kein Fehler aufgezeichnet.")
names = {9: "HardFault", 8: "MemManage", 7: "BusFault", 6: "UsageFault"}
print(f"{names.get(w[1], w[1])}, EXC_RETURN {w[2]:08x}, Rahmen bei {w[3]:08x}")
regs = ["r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr"]
for n, v in zip(regs, w[4:12]):
    where = ""
    if n in ("lr", "pc"):
        where = subprocess.run([f"{tc}/arm-none-eabi-addr2line", "-f", "-C", "-e", elf, hex(v & ~1)],
                               capture_output=True, text=True).stdout.split("\n")[0]
    print(f"  {n:4} {v:08x} {where}")
print(f"  Ausnahme im unterbrochenen Code: {w[11] & 0x1ff}")
print(f"CFSR {w[12]:08x}  HFSR {w[13]:08x}  MMFAR {w[14]:08x}  BFAR {w[15]:08x}")
PY
