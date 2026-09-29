// Inbetriebnahme des Boards: SDRAM, Display, Touch, Code im SDRAM, USB-Stick.
//
// Ablauf und Anzeige:
//   grüne LED an        Takt läuft
//   SDRAM-Test          schreibt und liest alle 8 MB; Fehler → rote LED blinkt 5×
//   Code im SDRAM       kleine Funktion ins SDRAM kopieren und ausführen
//   Display             16×16 Farbfelder = alle 256 Palettenindizes
//   USB-Stick           einbinden, Hauptverzeichnis lesen, 000.LFL anlesen
//   Touch               schwarzer Punkt mit weißem Rand, wo getippt wird
//   Ton                 solange der Bildschirm berührt wird: 440 Hz an PA5
// Ergebnisse stehen in `debug` am Anfang des RAM (per Debugger lesbar, z. B.
// openocd -f board/stm32f429discovery.cfg -c init -c "mdw 0x20000000 16").
#include <string.h>

#include <math.h>

#include "audio.h"
#include "board.h"
#include "ff.h"
#include "storage.h"

volatile struct {
  uint32_t magic;        // 0x544F5543 „TOUC“: Struktur gefunden
  uint32_t touches;      // Anzahl erkannter Berührungen
  uint16_t raw_x, raw_y;
  int16_t x, y;          // Panelkoordinaten
  uint32_t exec_result;  // 42 = Code im SDRAM lief
  uint32_t usb_state;    // 3 eingebunden, 4 fertig
  int32_t fat_result;    // FRESULT des letzten FatFs-Aufrufs
  uint32_t file_count;
  uint32_t lfl_size;     // Größe von 000.LFL
  uint8_t lfl_head[8];   // erste Bytes von 000.LFL
  char names[192];       // Dateinamen im Hauptverzeichnis, durch ';' getrennt
} debug = {.magic = 0x544F5543u};

static void sdram_test(void) {
  volatile uint32_t *mem = (volatile uint32_t *)SDRAM_BASE;
  const uint32_t words = SDRAM_SIZE / 4;
  for (uint32_t i = 0; i < words; ++i) mem[i] = i * 2654435761u;  // adressabhängiges Muster
  for (uint32_t i = 0; i < words; ++i) {
    if (mem[i] != i * 2654435761u) board_panic(5);
  }
}

// Thumb-Code für: movs r0, #42; bx lr
static void sdram_exec_test(void) {
  static const uint16_t code[] = {0x202A, 0x4770};
  uint16_t *dst = (uint16_t *)(SDRAM_BASE + SDRAM_SIZE - 64);
  memcpy(dst, code, sizeof(code));
  __DSB();
  __ISB();
  uint32_t (*fn)(void) = (uint32_t (*)(void))((uintptr_t)dst | 1u);
  debug.exec_result = fn();
}

// Testton 440 Hz aus einer Sinustabelle, halbe Aussteuerung; still, wenn
// niemand den Bildschirm berührt.
static int16_t sine[256];
static volatile bool tone_on;
static uint32_t tone_phase, tone_step;

static void tone_fill(int16_t *samples, unsigned count) {
  for (unsigned i = 0; i < count; ++i) {
    samples[i] = tone_on ? sine[tone_phase >> 24] : 0;
    tone_phase += tone_step;
  }
}

static void tone_init(void) {
  for (int i = 0; i < 256; ++i) sine[i] = (int16_t)(16383.0f * sinf(2.0f * 3.14159265f * i / 256.0f));
  audio_init(22050, tone_fill);
  tone_step = (uint32_t)(440.0 * 4294967296.0 / audio_rate());
}

static void fill_rect(uint8_t *fb, int x0, int y0, int w, int h, uint8_t color) {
  for (int y = y0; y < y0 + h; ++y) {
    if (y < 0 || y >= (int)LCD_HEIGHT) continue;
    for (int x = x0; x < x0 + w; ++x) {
      if (x >= 0 && x < (int)LCD_WIDTH) fb[y * LCD_WIDTH + x] = color;
    }
  }
}

// Testpalette: 0 schwarz, 255 weiß, dazwischen ein Farbkreis in 254 Stufen.
static void test_palette(void) {
  static uint8_t pal[256 * 3];
  for (int i = 1; i < 255; ++i) {
    int h = (i - 1) * 6 * 256 / 254;  // 0 … 6·256
    int f = h & 255, q = 255 - f;
    uint8_t r, g, b;
    switch (h >> 8) {
      case 0: r = 255; g = f; b = 0; break;
      case 1: r = q; g = 255; b = 0; break;
      case 2: r = 0; g = 255; b = f; break;
      case 3: r = 0; g = q; b = 255; break;
      case 4: r = f; g = 0; b = 255; break;
      default: r = 255; g = 0; b = q; break;
    }
    pal[3 * i] = r;
    pal[3 * i + 1] = g;
    pal[3 * i + 2] = b;
  }
  pal[255 * 3] = pal[255 * 3 + 1] = pal[255 * 3 + 2] = 255;
  lcd_set_palette(pal, 0, 256);
}

static void read_stick(void) {
  FRESULT r;
  debug.usb_state = 3;

  DIR dir;
  FILINFO info;
  unsigned used = 0;
  if ((r = f_opendir(&dir, "0:/")) == FR_OK) {
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
      debug.file_count++;
      size_t n = strlen(info.fname);
      if (used + n + 1 < sizeof(debug.names)) {
        memcpy((char *)debug.names + used, info.fname, n);
        used += n;
        debug.names[used++] = ';';
      }
    }
    f_closedir(&dir);
  }
  debug.fat_result = r;

  FIL f;
  if ((r = f_open(&f, "0:/000.LFL", FA_READ)) == FR_OK) {
    UINT got;
    debug.lfl_size = f_size(&f);
    r = f_read(&f, (void *)debug.lfl_head, sizeof(debug.lfl_head), &got);
    f_close(&f);
  }
  debug.fat_result = r;
  debug.usb_state = 4;
}

int main(void) {
  board_init();
  board_led(LED_GREEN, true);

  sdram_init();
  sdram_test();
  sdram_exec_test();

  uint8_t *fb = SDRAM_BASE;
  memset(fb, 0, LCD_WIDTH * LCD_HEIGHT);
  lcd_init(fb, 0, LCD_WIDTH, NULL);
  test_palette();
  for (int i = 0; i < 256; ++i) fill_rect(fb, (i % 16) * 15, (i / 16) * 20, 14, 19, (uint8_t)i);

  touch_init();
  tone_init();

  storage_init();

  bool stick_read = false;
  for (;;) {
    if (storage_poll() && !stick_read) {
      read_stick();
      stick_read = true;
    }

    uint16_t rx, ry;
    if (touch_read(&rx, &ry)) {
      int16_t x, y;
      touch_to_panel(rx, ry, &x, &y);
      debug.raw_x = rx;
      debug.raw_y = ry;
      debug.x = x;
      debug.y = y;
      debug.touches++;
      fill_rect(fb, x - 3, y - 3, 7, 7, 255);  // weißer Rand
      fill_rect(fb, x - 2, y - 2, 5, 5, 0);    // schwarzer Kern, auch auf Weiß sichtbar
      board_led(LED_RED, true);
      tone_on = true;
    } else {
      board_led(LED_RED, false);
      tone_on = false;
    }
  }
}
