// Laufzeitbasis des Spielprogramms im SDRAM: Vektortabelle, Heap, Logpuffer
// und die Systemaufrufe, die newlib erwartet.
#include <errno.h>
#include <reent.h>
#include <string.h>
#include <sys/stat.h>

#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "ff.h"
#include "runtime.h"

// --- Logpuffer ---------------------------------------------------------------------
//
// Ohne serielle Schnittstelle (der ST-LINK/V2 dieses Boards hat keinen
// virtuellen COM-Port) landen Meldungen in einem Ringpuffer im internen RAM.
// Auslesen per ST-LINK, z. B. mit tools/read-log.sh.

__attribute__((section(".ram_data"))) struct debug_log debug_log;

void log_write(const char *text, size_t len) {
  if (debug_log.magic != DEBUG_LOG_MAGIC) {
    memset(&debug_log, 0, sizeof(debug_log));
    debug_log.magic = DEBUG_LOG_MAGIC;
  }
  for (size_t i = 0; i < len; ++i) {
    debug_log.text[debug_log.head % sizeof(debug_log.text)] = text[i];
    debug_log.head++;
  }
}

void log_str(const char *text) {
  log_write(text, strlen(text));
}

// --- Vektortabelle ins SRAM ---------------------------------------------------------

extern uint32_t g_pfnVectors[];
#define VECTOR_COUNT (16 + 91)  // Kern + Interrupts des STM32F429

__attribute__((section(".ram_vectors"), aligned(512))) static uint32_t ram_vectors[VECTOR_COUNT];

void runtime_relocate_vectors(void) {
  memcpy(ram_vectors, g_pfnVectors, sizeof(ram_vectors));
  __DSB();
  SCB->VTOR = (uint32_t)ram_vectors;
  __DSB();
  __ISB();
}

// --- Code im internen SRAM ---------------------------------------------------------

extern uint32_t __ram_text_start[], __ram_text_end[], __ram_text_load[];
extern uint32_t __ram_bss_start[], __ram_bss_end[];

// Läuft über .preinit_array vor allen Konstruktoren – vor dem ersten Aufruf
// von Code aus .ram_text und vor dem ersten Zugriff auf .ram_bss (siehe
// stm32f429zi_sdram.ld).
static void copy_ram_text(void) {
  memcpy(__ram_text_start, __ram_text_load, (size_t)((char *)__ram_text_end - (char *)__ram_text_start));
  memset(__ram_bss_start, 0, (size_t)((char *)__ram_bss_end - (char *)__ram_bss_start));
  __DSB();
  __ISB();
}
__attribute__((section(".preinit_array"), used)) static void (*const copy_ram_text_entry)(void) = copy_ram_text;

// --- newlib-Systemaufrufe ------------------------------------------------------------

extern char __heap_start[];
extern char __sdram_end[];
static char *heap_top = __heap_start;

void *_sbrk(ptrdiff_t incr) {
  if (heap_top + incr > __sdram_end) {
    errno = ENOMEM;
    return (void *)-1;
  }
  char *prev = heap_top;
  heap_top += incr;
  return prev;
}

size_t runtime_heap_used(void) {
  return (size_t)(heap_top - __heap_start);
}

int _write(int fd, const char *buf, int len) {
  (void)fd;
  log_write(buf, (size_t)len);
  return len;
}

int _read(int fd, char *buf, int len) {
  (void)fd;
  (void)buf;
  (void)len;
  return 0;
}

int _close(int fd) {
  (void)fd;
  return -1;
}

int _lseek(int fd, int off, int whence) {
  (void)fd;
  (void)off;
  (void)whence;
  return 0;
}

int _fstat(int fd, struct stat *st) {
  (void)fd;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int fd) {
  (void)fd;
  return 1;
}

// remove() – ScummVM löscht damit Spielstände. Pfade sind die der
// Dateisystem-Fabrik („/SAVES/…“), auf dem Stick also Laufwerk „0:“.
int _unlink(const char *path) {
  char full[260];
  if (strlen(path) + 3 > sizeof(full)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  strcpy(full, "0:");
  strcat(full, path);
  FRESULT r = f_unlink(full);
  if (r == FR_OK) return 0;
  errno = (r == FR_NO_FILE || r == FR_NO_PATH) ? ENOENT : EIO;
  return -1;
}

int _getpid(void) {
  return 1;
}

int _kill(int pid, int sig) {
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return -1;
}

void _exit(int status) {
  (void)status;
  log_str("\n[exit]\n");
  board_panic(1);
}

// --- FreeRTOS ---------------------------------------------------------------------

// newlib-malloc ist nicht wiedereintrittsfähig; ScummVM und der Audio-Task
// holen beide Speicher. Vor dem Start des Schedulers gibt es nur einen Faden
// (und vTaskSuspendAll wäre dort noch nicht erlaubt).
void __malloc_lock(struct _reent *r) {
  (void)r;
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) vTaskSuspendAll();
}

void __malloc_unlock(struct _reent *r) {
  (void)r;
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) (void)xTaskResumeAll();
}

void rtos_assert_failed(const char *file, int line) {
  char text[16];
  log_str("FreeRTOS-Assert ");
  log_str(file);
  log_str(":");
  int n = 0;
  char digits[12];
  do {
    digits[n++] = (char)('0' + line % 10);
    line /= 10;
  } while (line && n < 11);
  for (int i = 0; i < n; ++i) text[i] = digits[n - 1 - i];
  text[n] = '\n';
  log_write(text, (size_t)n + 1);
  board_panic(3);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name) {
  (void)task;
  log_str("Stack-Ueberlauf im Task ");
  log_str(name);
  log_str("\n");
  board_panic(5);
}
