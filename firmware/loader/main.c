// Lader im Flash: richtet Takt, SDRAM und MPU ein, lädt das Spielprogramm
// MONKEY.BIN vom USB-Stick (USB USER) nach 0xD0040000 und startet es.
//
// Anzeige (Panel hochkant, Balken wächst von unten nach oben):
//   dunkelblau          warte auf den Stick
//   Balken grün         MONKEY.BIN wird gelesen
//   rot + LED blinkt    Fehler: 2× kein Stick, 4× MONKEY.BIN fehlt oder
//                       ist unlesbar, 5× Datei passt nicht / kein Programm
#include <string.h>

#include "board.h"
#include "ff.h"
#include "storage.h"

#define IMAGE_BASE ((uint8_t *)0xD0040000u)
#define IMAGE_MAX  (SDRAM_SIZE - 0x40000u)
#define STICK_TIMEOUT_MS 10000u

enum { COL_BLACK, COL_BLUE, COL_GREEN, COL_RED };

static uint8_t *const fb = SDRAM_BASE;  // L8, 240×320, vor dem Programm

static void screen(uint8_t color) {
  memset(fb, color, LCD_WIDTH * LCD_HEIGHT);
}

static void progress(uint32_t done, uint32_t total) {
  const uint32_t rows = (uint32_t)((uint64_t)done * LCD_HEIGHT / total);
  memset(fb + (LCD_HEIGHT - rows) * LCD_WIDTH, COL_GREEN, rows * LCD_WIDTH);
}

static BOARD_NORETURN void fail(int code) {
  screen(COL_RED);
  board_panic(code);
}

// Programmstart wie nach einem Reset: Stapelzeiger und Einsprung aus der
// Vektortabelle des Programms. Kein HAL_DeInit – das setzte auch den FMC
// zurück, und das SDRAM ist jetzt der Programmspeicher.
// noinline: tools/run-game.sh springt hierher (r0 = Programmadresse).
__attribute__((noinline)) static BOARD_NORETURN void start_image(const uint32_t *vectors) {
  __disable_irq();
  SysTick->CTRL = 0;
  for (unsigned i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); ++i) {
    NVIC->ICER[i] = 0xFFFFFFFFu;
    NVIC->ICPR[i] = 0xFFFFFFFFu;
  }
  __DSB();
  __ISB();
  __asm volatile("msr msp, %0\n bx %1" : : "r"(vectors[0]), "r"(vectors[1]));
  __builtin_unreachable();
}

// Eigene Funktion, damit tools/run-game.sh hier anhalten und das Programm
// per ST-LINK statt vom Stick laden kann.
__attribute__((noinline)) static void load_from_stick(void) {
  storage_init();
  if (!storage_wait(STICK_TIMEOUT_MS)) fail(2);

  FIL f;
  if (f_open(&f, "0:/MONKEY.BIN", FA_READ) != FR_OK) fail(4);
  const uint32_t size = f_size(&f);
  if (size < 8 || size > IMAGE_MAX) fail(5);
  for (uint32_t done = 0; done < size;) {
    UINT got;
    const UINT chunk = size - done < 32768u ? size - done : 32768u;
    if (f_read(&f, IMAGE_BASE + done, chunk, &got) != FR_OK || got != chunk) fail(4);
    done += got;
    progress(done, size);
  }
  f_close(&f);
  storage_deinit();
}

int main(void) {
  board_init();
  board_led(LED_GREEN, true);
  sdram_init();

  static const uint8_t palette[] = {0, 0, 0, 0, 0, 96, 0, 200, 0, 220, 0, 0};
  screen(COL_BLUE);
  lcd_init(fb, 0, LCD_WIDTH, NULL);
  lcd_set_palette(palette, 0, sizeof(palette) / 3);

  load_from_stick();

  // Plausibel? Stapel im internen SRAM, Einsprung (Thumb) im Programm.
  const uint32_t *vectors = (const uint32_t *)IMAGE_BASE;
  const uint32_t sp = vectors[0], pc = vectors[1];
  if (sp < SRAM_BASE || sp > SRAM_BASE + 192u * 1024u) fail(5);
  if (!(pc & 1u) || pc < (uint32_t)IMAGE_BASE || pc >= (uint32_t)IMAGE_BASE + IMAGE_MAX) fail(5);

  start_image(vectors);
}
