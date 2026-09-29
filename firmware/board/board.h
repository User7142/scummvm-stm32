// Board-Schicht für das STM32F429I-Discovery.
//
// Eigene, schlanke Treiber auf Basis der ST-HAL statt des ST-BSP: Das
// BSP-Paket (32f429idiscovery-bsp v2.1.8) erwartet die alten
// Komponententreiber und die „Utilities“-Schriften, die separat
// veröffentlichten Komponenten (v3) haben eine neue Schnittstelle. Die
// Hardwarewerte (Pins, Timings, Init-Sequenzen) sind 1:1 aus dem BSP
// übernommen und in den .c-Dateien jeweils vermerkt.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#define BOARD_NORETURN [[noreturn]]
#else
#define BOARD_NORETURN _Noreturn
#endif

// Systemtakt 168 MHz aus HSE 8 MHz. Nicht 180 MHz: Der USB-Host braucht
// exakt 48 MHz, und die liefert beim F429 nur der Haupt-PLL (PLLQ) – bei
// VCO 336 MHz ergibt das 168 MHz Systemtakt und 48 MHz für USB.
// Vollständig: HAL, LEDs, Takt (Lader und Inbetriebnahme).
void board_init(void);
// Für das Spielprogramm im SDRAM: Takt, SDRAM und MPU hat der Lader schon
// eingerichtet (und das SDRAM darf nicht neu initialisiert werden, während
// Code daraus läuft) – nur HAL-Zeitbasis und LEDs.
void board_init_runtime(void);

enum { LED_GREEN, LED_RED };
void board_led(int led, bool on);

// Blaue Taste USER (B1, PA0): gedrückt = high, extern entprellt (RC-Glied).
void button_init(void);
bool button_pressed(void);

// Fehler, aus dem nichts mehr rettet: rote LED blinkt den Code (1–9).
BOARD_NORETURN void board_panic(int code);

// Letzter Prozessorfehler (HardFault, MemManage, BusFault, UsageFault), für
// den Debugger: tools/read-fault.sh.
#define BOARD_FAULT_MAGIC 0x544C5546u  // „FULT“
struct board_fault {
  uint32_t magic, code, exc_return, sp;
  uint32_t frame[8];  // r0, r1, r2, r3, r12, lr, pc, xpsr
  uint32_t cfsr, hfsr, mmfar, bfar;
};
extern volatile struct board_fault board_fault;

// --- SDRAM (IS42S16400J, 8 MB, FMC Bank 2) ---------------------------------
// Der Bereich 0xC0000000–0xDFFFFFFF ist in der Standard-Speicherkarte des
// Cortex-M4 „Device“ und nicht ausführbar. sdram_init() legt deshalb eine
// MPU-Region über die 8 MB: normaler Speicher, ausführbar – so kann das
// Spielprogramm im SDRAM laufen. (Das Umblenden per SYSCFG_MEMRMP.SWP_FMC
// taugt hier nicht: am Board gemessen erscheint Bank 2 danach nirgends.)
#define SDRAM_BASE ((uint8_t *)0xD0000000u)
#define SDRAM_SIZE (8u * 1024u * 1024u)
void sdram_init(void);

// --- Display (ILI9341, 240×320, LTDC) ----------------------------------------
// Ebene 0 läuft im Format L8: ein Byte je Pixel, Farbe aus der CLUT.
// Ebene 1 (optional) ist RGB565 und liegt darüber; sie ist aus, bis
// lcd_show_overlay() sie einschaltet (ScummVM-Menüs).
// Das Panel steht hochkant; die Koordinaten hier sind die des Panels.
#define LCD_WIDTH  240u
#define LCD_HEIGHT 320u
// Ebene 0 belegt nur die Panelspalten x0 … x0+width−1 (Rest zeigt die
// schwarze Hintergrundfarbe des LTDC); ihr Bildspeicher hat width Byte je Zeile.
void lcd_init(uint8_t *framebuffer, unsigned x0, unsigned width, uint16_t *overlay);
void lcd_show_overlay(bool on);
// Bild von Ebene 0 (Bildspeicher aus lcd_init, width Byte je Zeile, height
// Zeilen) mit der linken oberen Ecke an Panelposition (x0, y0) zeigen. Was
// über den Rand ragt, wird abgeschnitten; freie Fläche zeigt den schwarzen
// Hintergrund. Wirksam ab dem nächsten Bild.
void lcd_place_layer0(int x0, int y0, unsigned width, unsigned height);
// palette: 256 Einträge à R, G, B (0–255)
void lcd_set_palette(const uint8_t *palette, unsigned first, unsigned count);

// --- Touch (STMPE811, I²C3) --------------------------------------------------
void touch_init(void);
// Rohwerte 0–4095 des Touch-Controllers; false, wenn nicht berührt.
bool touch_read(uint16_t *raw_x, uint16_t *raw_y);
// Rohwerte → Panelkoordinaten (0–239, 0–319), auf den Rand begrenzt.
void touch_to_panel(uint16_t raw_x, uint16_t raw_y, int16_t *x, int16_t *y);

#ifdef __cplusplus
}
#endif
