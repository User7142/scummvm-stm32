# The Secret of Monkey Island on the STM32F429I-Discovery

*The Secret of Monkey Island*, running completely on the STM32F429I-Discovery board:
the SCUMM engine of [ScummVM](https://www.scummvm.org/), ported to the Cortex-M4 with
FreeRTOS, in landscape mode on the 2.4 inch display, controlled via touch, with AdLib
music through the DAC. The game data comes from **your own copy** of the game
(VGA floppy version) on a USB stick.

**Status: playable.** Intro, rooms, menus and music work; room changes without audio dropouts.

Photos, a video and the background story: [palm2000.com](https://palm2000.com/articles/48)

> **No game data included.** *The Secret of Monkey Island* is © Lucasfilm Games / Disney.
> This repository contains only the engine port and the board support code. You need your
> own copy of the game, exactly like with ScummVM on a PC.

---

## Controls

Hold the board in **landscape** orientation, with the ST-LINK connector on the left.

| Input | Action |
|---|---|
| Tap | Left click at this position (verbs, items, walking) |
| Drag | The mouse pointer follows the finger |
| USER button (blue), short | Escape: skip a cutscene |
| USER button, long (0.8 s) | ScummVM menu: save, load, options |
| RESET button (black) | Restart; the loader loads the game from the stick again |

Saved games are stored on the stick in `/SAVES`, the settings in `/SCUMMVM.INI`.

## Hardware

| | STM32F429I-Discovery (MB1075) |
|---|---|
| CPU | STM32F429ZI, Cortex-M4F, **168 MHz** (USB needs 48 MHz from the PLL) |
| Memory | 2 MB flash, 192 KB SRAM + 64 KB CCM, 8 MB SDRAM (FMC bank 2, `0xD0000000`) |
| Display | 2.4 inch, 240×320 portrait, ILI9341 on the LTDC |
| Touch | resistive, STMPE811 on I²C3 |
| USB | **USB ST-LINK** (Mini-USB, top): power, flashing, debugging · **USB USER** (Micro-USB OTG, bottom): USB stick |

### USB stick (FAT32, root directory)

| File | Content |
|---|---|
| `000.LFL`, `901.LFL` … `904.LFL` | Game files from your own copy (VGA floppy version) |
| `DISK01.LEC` … `DISK04.LEC` | Game files from your own copy (VGA floppy version) |
| `MONKEY.BIN` | The game program (`firmware/build/MONKEY.BIN`) |

The `._*` files that macOS creates on FAT sticks do no harm; the file system hides dot files.

### Audio output (line level on PA5)

The sound comes from DAC channel 2 on **PA5** (lower pin header P1, inner pin directly
above the "PA4" label). For active speakers:

```
 PA5 --- 100 Ohm --- (+) 10 uF (-) --+----------+---- jack tip    (left)
                                     |          +---- jack ring   (right)
                                   10 kOhm
                                     |
 GND --------------------------------+--------------- jack sleeve
```

The 100 Ω resistor limits the level so the speakers are not overdriven. The capacitor
blocks the DC voltage (the DAC rests at about 1.45 V), and the 10 kΩ resistor keeps the
output at ground level (no crackling when plugging in). The DAC output is designed for
loads of 5 kΩ and more; headphones work, but they are quiet.

## Architecture

```
 Flash (0x08000000)          SDRAM (0xD0000000, 8 MB)              SRAM (0x20000000, 192 KB)
 ┌──────────────┐            ┌─────────────────────────┐           ┌─────────────────────────┐
 │ Loader 29 KB │ ──loads──▶ │ Frame buffers L8+RGB565 │           │ Audio code + OPL tables │
 └──────────────┘            │ MONKEY.BIN (2.7 MB)     │ ─copies─▶ │ Vector table, log       │
                             │   ScummVM + backend     │           │ Task stacks (96 + 12 KB)│
                             │ Heap up to the end      │           │ Interrupt stack         │
                             └─────────────────────────┘           └─────────────────────────┘
```

- **Loader in flash** (`firmware/loader`): starts clock, SDRAM, MPU, USB host and FatFs,
  loads `MONKEY.BIN` from the stick to `0xD0040000` and jumps into it. Display:
  blue = waiting for the stick, green bar = loading, red = error (see below).
- **Code in SDRAM.** ScummVM with the SCUMM engine is larger than the flash. On the
  Cortex-M4, the range `0xC0000000`–`0xDFFFFFFF` is "device" memory and not executable;
  an MPU region turns the 8 MB into normal, executable memory. (Remapping via
  `SYSCFG_MEMRMP.SWP_FMC` does not work on this board: bank 2 does not show up anywhere afterwards.)
- **Hot path in SRAM.** The SDRAM has no cache and is 16 bits wide. The AdLib emulation
  (DOSBox OPL), the mixer and the rate converter therefore live in `.ram_text`/`.ram_bss`
  and are copied to SRAM before the constructors run (`.preinit_array`).
- **FreeRTOS** (`game/FreeRTOSConfig.h`): ScummVM runs as one task, the mixer in its own
  task with higher priority, woken by the DMA interrupt of the audio output. ScummVM
  mutexes are recursive FreeRTOS mutexes with priority inheritance, so loading a room
  (ScummVM holds the resource mutex for up to 400 ms) does not stall the audio. The HAL
  time base runs on TIM7; FreeRTOS owns the SysTick.
- **Display** (`scummvm/ltdc-graphics.*`): the game picture (320×200, 256 colours) is
  rotated by 90° into an L8 layer (4×4 blocks, transposed in registers); the VGA palette
  lives in the LTDC colour lookup table. ScummVM's menus draw into a second layer
  (RGB565, 320×240). Screen shaking only moves the layer window.
- **Audio** (`board/audio.*`, `scummvm/stm32-mixer.*`): DAC2 on PA5, TIM6 as sample
  clock, double-buffered DMA, 11025 Hz mono. The emulation needs about 35 % of the CPU;
  at 22050 Hz it was around 70 %.
- **Files** (`scummvm/fatfs-fs.*`): ScummVM file system on top of FatFs; writes (saved
  games) are atomic via `.tmp` and rename.

## Building

Requirements: Arm GNU Toolchain 15.3 (`firmware/Makefile`, variable `TOOLCHAIN`), stlink, OpenOCD.

```bash
git clone --recurse-submodules https://github.com/User7142/monkey-island-stm32.git
cd monkey-island-stm32/firmware
make                 # configures and builds ScummVM (takes a while the first time), loader, MONKEY.BIN
make flash           # writes the loader to the flash
cp -X build/MONKEY.BIN /Volumes/<stick>/
```

Further targets: `make bringup` / `make flash-bringup` builds the bring-up program
(SDRAM test, colour pattern, touch point, 440 Hz test tone when touched).

### Development without copying to the stick

```bash
tools/run-game.sh    # load MONKEY.BIN into SDRAM via ST-LINK and start it (~30 s)
tools/read-log.sh    # game log (ring buffer in SRAM): ScummVM messages,
                     # audio load every 10 s, underruns, free stack, heap
tools/read-fault.sh  # decode the last processor fault (frame, CFSR …)
```

### Error codes (red LED blinks n times)

| n | Meaning |
|---|---|
| 2 | Loader: no USB stick detected |
| 3 | Display or FreeRTOS assert (details in the log) |
| 4 | Loader: `MONKEY.BIN` missing or unreadable · Game: touch controller does not respond |
| 5 | Loader: `MONKEY.BIN` is not a valid program · Game: stack overflow |
| 6–9 | Processor fault (usage, bus, MemManage, HardFault) → `tools/read-fault.sh` |

## Known limits

- No battery-backed clock: all saved games carry the same date.
- Music only via AdLib. A real MT-32 could be connected via a UART MIDI output (not
  built); the Cortex-M4 cannot emulate one.

## Libraries (Git submodules, shallow, fixed releases)

| Path | Source | Version |
|---|---|---|
| `lib/scummvm` | scummvm/scummvm | v2026.3.0 |
| `lib/freertos` | FreeRTOS/FreeRTOS-Kernel | V11.3.1 |
| `lib/stm32f4xx_hal_driver` | STMicroelectronics | v1.8.5 |
| `lib/cmsis_core`, `lib/cmsis_device_f4` | STMicroelectronics | v5.9.0_20250520, v2.6.11 |
| `lib/usb_host` | stm32_mw_usb_host | v3.5.5 |
| `lib/fatfs` | stm32_mw_fatfs | r0.16_stm32cube_20260904 |

The board drivers (display, touch, SDRAM, audio) are small drivers of their own on top of
the HAL (`firmware/board`); the hardware values come from the ST BSP.

## License

ScummVM is licensed under the GNU GPL v3, and so is this port (see [LICENSE](LICENSE)).
FreeRTOS is licensed under the MIT license; the ST libraries under their respective licenses.

*The Secret of Monkey Island* is © Lucasfilm Games / Disney. This is a private fan
project; no game data is included.
