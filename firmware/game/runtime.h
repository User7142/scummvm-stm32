// Laufzeitbasis des Spielprogramms: Logpuffer, Vektortabelle, Heap.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEBUG_LOG_MAGIC 0x474F4C44u  // „DLOG“

struct debug_log {
  uint32_t magic;
  uint32_t head;  // Anzahl je geschriebener Zeichen (Position = head % Größe)
  char text[16384];
};
extern struct debug_log debug_log;

void log_write(const char *text, size_t len);
void log_str(const char *text);

// Vektortabelle ins interne SRAM kopieren und VTOR umstellen.
void runtime_relocate_vectors(void);
size_t runtime_heap_used(void);

#ifdef __cplusplus
}
#endif
