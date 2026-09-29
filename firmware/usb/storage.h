// USB-Stick am Port USB USER: Host-Stack, Einbinden per FatFs.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Host-Stack starten. Danach regelmäßig storage_poll() aufrufen.
void storage_init(void);
// Arbeitet den USB-Host-Zustandsautomaten ab; bindet den Stick ein, sobald
// er bereit ist. Liefert true, solange ein Stick eingebunden ist.
bool storage_poll(void);
// Wartet bis zu timeout_ms auf einen eingebundenen Stick.
bool storage_wait(uint32_t timeout_ms);
// Host-Stack anhalten und Peripherie freigeben (vor dem Sprung ins Spiel).
void storage_deinit(void);

#ifdef __cplusplus
}
#endif
