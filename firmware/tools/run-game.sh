#!/usr/bin/env bash
# Spielprogramm per ST-LINK ins SDRAM laden und starten – ohne Umweg über
# den USB-Stick (der steckt am Board, nicht am Mac).
#
# Der Lader im Flash läuft bis zum Einlesen vom Stick (load_from_stick);
# bis dahin hat er Takt, SDRAM, MPU und Display eingerichtet. Dort hält
# openocd an, schreibt build/MONKEY.BIN nach 0xD0040000 und setzt beim
# regulären Startpfad des Laders (start_image) fort.
#
# Aufruf aus firmware/: tools/run-game.sh   (vorher: make && make flash)
set -euo pipefail
cd "$(dirname "$0")/.."
TOOLCHAIN="${TOOLCHAIN:-$HOME/tools/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi/bin}"
NM="$TOOLCHAIN/arm-none-eabi-nm"

sym() {
  local addr
  addr=$("$NM" build/loader.elf | awk -v s="$1" '$3 == s || index($3, s ".") == 1 { print $1; exit }')
  [ -n "$addr" ] || { echo "Symbol $1 fehlt in build/loader.elf" >&2; exit 1; }
  echo "0x$addr"
}
LOAD=$(sym load_from_stick)
START=$(sym start_image)

openocd -f board/stm32f429discovery.cfg \
  -c "init" -c "reset halt" \
  -c "bp $LOAD 2 hw" -c "resume" -c "wait_halt 15000" -c "rbp $LOAD" \
  -c "load_image build/MONKEY.BIN 0xD0040000 bin" \
  -c "reg r0 0xD0040000" -c "reg pc $START" -c "resume" -c "exit"
