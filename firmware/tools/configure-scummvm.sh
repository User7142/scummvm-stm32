#!/bin/bash
# Konfiguriert ScummVM (lib/scummvm) für das STM32F429I-Discovery.
#
# Nur die SCUMM-Engine v0–v6 (ohne HE und v7/v8), keine optionalen
# Bibliotheken, Release-Modus mit -Os. ScummVMs eigenes Backend („null“) wird
# nicht verwendet: Aus diesem Build nehmen wir nur die Modulbibliotheken
# (common, engines/scumm, gui, …); das Backend und das Linken macht
# firmware/Makefile.
#
# Aufruf: tools/configure-scummvm.sh <build-verzeichnis> <toolchain-bin>
set -euo pipefail
BUILD_DIR="$1"
TOOLCHAIN="$2"
SRC="$(cd "$(dirname "$0")/../../lib/scummvm" && pwd)"

CPU="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
export PATH="$TOOLCHAIN:$PATH"
export CXX=arm-none-eabi-g++
# ar/ranlib von macOS können für ARM-ELF-Objekte keinen Symbolindex anlegen.
export AR=arm-none-eabi-ar  # configure hängt „cr“ selbst an
export RANLIB=arm-none-eabi-ranlib
# configure erkennt die Byte-Reihenfolge mit strings; es sucht sonst
# „arm-eabi-strings“ und nimmt ersatzweise das strings von macOS (kein ELF).
export STRINGS=arm-none-eabi-strings
# -Os statt des -O2, das --enable-release anhängen würde (daher
# --enable-release-mode --disable-optimizations).
export CXXFLAGS="$CPU -Os -ffunction-sections -fdata-sections -fno-exceptions"
# Nur für die Linktests von configure; kein --gc-sections, das würde deren
# Prüfdaten verwerfen (Endianness-Test).
export LDFLAGS="$CPU --specs=nosys.specs"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
"$SRC/configure" --host=arm-none-eabi --backend=null \
  --disable-all-engines --enable-engine=scumm --disable-engine=scumm-7-8,he \
  --disable-detection-full --enable-release-mode --disable-optimizations --disable-debug \
  --disable-mt32emu --disable-seq-midi --disable-timidity --disable-lua \
  --disable-16bit --disable-highres --disable-scalers --disable-aspect \
  --disable-translation --disable-taskbar --disable-cloud --disable-system-dialogs \
  --disable-eventrecorder --disable-tts --disable-bink --disable-tinygl \
  --disable-ogg --disable-vorbis --disable-tremor --disable-mad --disable-fribidi \
  --disable-flac --disable-zlib --disable-jpeg --disable-png --disable-gif \
  --disable-theoradec --disable-vpx --disable-faad --disable-fluidsynth \
  --disable-fluidlite --disable-sonivox --disable-freetype2 --disable-sdlnet \
  --disable-libcurl --disable-enet --disable-discord --disable-opengl-game \
  --disable-readline --disable-gtk
