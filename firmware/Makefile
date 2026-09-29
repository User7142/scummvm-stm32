# Firmware für das STM32F429I-Discovery
#
#   make                 Lader (build/loader.bin) und Spielprogramm (build/MONKEY.BIN)
#   make flash           Lader per ST-LINK in den Flash schreiben
#   make game            nur das Spielprogramm (läuft im SDRAM); konfiguriert
#                        und baut dafür ScummVM in build/scummvm
#   make bringup         Inbetriebnahme-Programm (build/bringup.bin)
#   make flash-bringup   … in den Flash schreiben
#   make compile_commands.json   Include-Pfade für clangd/IDE
#
# Toolchain: Arm GNU Toolchain (arm-none-eabi), Pfad über TOOLCHAIN.

TOOLCHAIN ?= $(HOME)/tools/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi/bin
CC        := $(TOOLCHAIN)/arm-none-eabi-gcc
CXX       := $(TOOLCHAIN)/arm-none-eabi-g++
OBJCOPY   := $(TOOLCHAIN)/arm-none-eabi-objcopy
SIZE      := $(TOOLCHAIN)/arm-none-eabi-size

LIB       := ../lib
HAL       := $(LIB)/stm32f4xx_hal_driver
BUILD     := build

CPU       := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
DEFS      := -DSTM32F429xx -DUSE_HAL_DRIVER
USBH      := $(LIB)/usb_host
FATFS     := $(LIB)/fatfs/source
INCLUDES  := -Isystem -Iboard -Iusb -I$(HAL)/Inc -I$(LIB)/cmsis_device_f4/Include -I$(LIB)/cmsis_core/Include \
             -I$(USBH)/Core/Inc -I$(USBH)/Class/MSC/Inc -I$(FATFS) -I$(FATFS)/drivers/usb_host
CFLAGS    := $(CPU) $(DEFS) $(INCLUDES) -std=gnu11 -Os -g3 -Wall -Wextra -Wno-unused-parameter \
             -ffunction-sections -fdata-sections
LDFLAGS   := $(CPU) -Tsystem/stm32f429zi_flash.ld --specs=nano.specs --specs=nosys.specs \
             -Wl,--gc-sections -Wl,--print-memory-usage
LDLIBS    := -lm

HAL_SRC   := $(addprefix $(HAL)/Src/stm32f4xx_, hal.c hal_cortex.c hal_rcc.c hal_rcc_ex.c hal_gpio.c \
             hal_pwr.c hal_pwr_ex.c hal_sdram.c ll_fmc.c hal_ltdc.c hal_spi.c hal_i2c.c hal_dma.c \
             hal_hcd.c ll_usb.c)
USB_SRC   := $(addprefix $(USBH)/Core/Src/usbh_, core.c ctlreq.c ioreq.c pipes.c) \
             $(addprefix $(USBH)/Class/MSC/Src/usbh_, msc.c msc_bot.c msc_scsi.c) \
             $(addprefix $(FATFS)/, ff.c ffunicode.c diskio.c ff_gen_drv.c drivers/usb_host/usbh_diskio.c) \
             $(wildcard usb/*.c)
SYS_SRC   := $(LIB)/cmsis_device_f4/Source/Templates/system_stm32f4xx.c
STARTUP   := $(LIB)/cmsis_device_f4/Source/Templates/gcc/startup_stm32f429xx.s
BOARD_SRC := $(wildcard board/*.c)

BRINGUP_SRC := bringup/main.c $(BOARD_SRC) $(USB_SRC) $(HAL_SRC) $(SYS_SRC)
BRINGUP_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(subst ../,,$(BRINGUP_SRC))) $(BUILD)/startup.o
LOADER_SRC  := loader/main.c $(BOARD_SRC) $(USB_SRC) $(HAL_SRC) $(SYS_SRC)
LOADER_OBJ  := $(patsubst %.c,$(BUILD)/%.o,$(subst ../,,$(LOADER_SRC))) $(BUILD)/startup.o

# --- Spielprogramm: ScummVM (nur SCUMM-Engine) + Backend, gelinkt fürs SDRAM ---

SCUMMVM     := $(LIB)/scummvm
SCUMMVM_B   := $(BUILD)/scummvm
# Reihenfolge egal, gelinkt wird als Gruppe.
SCUMMVM_LIBS := $(addprefix $(SCUMMVM_B)/, engines/scumm/libscumm.a engines/libengines.a gui/libgui.a \
             graphics/libgraphics.a image/libimage.a video/libvideo.a audio/libaudio.a math/libmath.a \
             backends/libbackends.a common/formats/libformats.a common/compression/libcompression.a \
             common/libcommon.a)
# Einzelobjekte: Programmkern und die Spielerkennung (bei statischen Engines
# gehört sie direkt ins Programm, ScummVM nennt das DETECT_OBJS).
SCUMMVM_BASE := $(addprefix $(SCUMMVM_B)/base/, main.o commandLine.o plugins.o version.o) \
             $(SCUMMVM_B)/engines/scumm/detection.o
# Defines wie im ScummVM-Makefile und in build/scummvm/config.mk, ohne die Installationspfade.
SCUMMVM_DEFS := -DHAVE_CONFIG_H -DRELEASE_BUILD -DARM_TARGET -DUSE_NULL_DRIVER
FREERTOS    := $(LIB)/freertos
GAME_INCLUDES := -Igame -Iscummvm -I$(FREERTOS)/include -I$(FREERTOS)/portable/GCC/ARM_CM4F
CXXFLAGS  := $(CPU) $(DEFS) $(SCUMMVM_DEFS) $(INCLUDES) $(GAME_INCLUDES) -isystem $(SCUMMVM) -isystem $(SCUMMVM)/engines \
             -isystem $(SCUMMVM_B) -std=c++11 -Os -g3 -Wall -Wextra -Wno-unused-parameter -fno-exceptions \
             -ffunction-sections -fdata-sections
# Volle newlib statt nano: ScummVM formatiert auch Gleitkommazahlen.
GAME_LDFLAGS := $(CPU) -Tsystem/stm32f429zi_sdram.ld --specs=nosys.specs -Wl,--gc-sections \
             -Wl,--print-memory-usage

FREERTOS_SRC := $(addprefix $(FREERTOS)/, tasks.c queue.c list.c portable/GCC/ARM_CM4F/port.c \
             portable/MemMang/heap_3.c)
GAME_CSRC   := $(wildcard game/*.c) $(FREERTOS_SRC) $(BOARD_SRC) $(USB_SRC) $(HAL_SRC) $(SYS_SRC)
GAME_CXXSRC := game/main.cpp $(wildcard scummvm/*.cpp)
GAME_OBJ    := $(patsubst %.c,$(BUILD)/%.o,$(subst ../,,$(GAME_CSRC))) \
               $(patsubst %.cpp,$(BUILD)/%.o,$(GAME_CXXSRC)) $(BUILD)/startup.o
# Nur Spielprogramm und FreeRTOS sehen die RTOS-Header (FreeRTOSConfig.h in game/).
$(patsubst %.c,$(BUILD)/%.o,$(subst ../,,$(wildcard game/*.c) $(FREERTOS_SRC))): CFLAGS += $(GAME_INCLUDES)

.PHONY: all flash flash-bringup bringup clean game scummvm
all: $(BUILD)/loader.bin $(BUILD)/MONKEY.BIN
game: $(BUILD)/MONKEY.BIN
bringup: $(BUILD)/bringup.bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/lib/%.o: ../lib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(BUILD)/startup.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -c $< -o $@

$(BUILD)/bringup.elf: $(BRINGUP_OBJ) system/stm32f429zi_flash.ld
	$(CC) $(LDFLAGS) -Wl,-Map=$(BUILD)/bringup.map $(BRINGUP_OBJ) $(LDLIBS) -o $@
	$(SIZE) $@

$(BUILD)/loader.elf: $(LOADER_OBJ) system/stm32f429zi_flash.ld
	$(CC) $(LDFLAGS) -Wl,-Map=$(BUILD)/loader.map $(LOADER_OBJ) $(LDLIBS) -o $@
	$(SIZE) $@

$(SCUMMVM_B)/config.mk: tools/configure-scummvm.sh
	tools/configure-scummvm.sh $(SCUMMVM_B) $(TOOLCHAIN)

# ScummVMs eigenes Makefile baut die Bibliotheken; version.o hängt dort am
# fertigen Programm, das Null-Backend bauen wir aber nicht (-o).
scummvm: $(SCUMMVM_B)/config.mk
	PATH="$(TOOLCHAIN):$$PATH" $(MAKE) -C $(SCUMMVM_B) \
	  $(patsubst $(SCUMMVM_B)/%,%,$(SCUMMVM_LIBS) $(filter-out %/version.o,$(SCUMMVM_BASE)))
	PATH="$(TOOLCHAIN):$$PATH" $(MAKE) -C $(SCUMMVM_B) \
	  -o backends/platform/null/null.o -o backends/platform/null/libnull.a base/version.o

$(SCUMMVM_LIBS) $(SCUMMVM_BASE): scummvm

$(BUILD)/game.elf: $(GAME_OBJ) $(SCUMMVM_LIBS) $(SCUMMVM_BASE) system/stm32f429zi_sdram.ld
	$(CXX) $(GAME_LDFLAGS) -Wl,-Map=$(BUILD)/game.map $(GAME_OBJ) $(SCUMMVM_BASE) \
	  -Wl,--start-group $(SCUMMVM_LIBS) -lstdc++ -lm -lc -Wl,--end-group -o $@
	$(SIZE) $@

$(BUILD)/MONKEY.BIN: $(BUILD)/game.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

flash: $(BUILD)/loader.bin
	st-flash --reset write $< 0x08000000

flash-bringup: $(BUILD)/bringup.bin
	st-flash --reset write $< 0x08000000

compile_commands.json: Makefile
	@python3 -c 'import json,os,sys; d=os.getcwd(); f=sys.argv[1]; \
	  print(json.dumps([{"directory": d, "file": s, "arguments": ["$(CC)"] + f.split() + ["-c", s]} for s in sys.argv[2:]], indent=1))' \
	  "$(CFLAGS)" $(BRINGUP_SRC) > $@

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
