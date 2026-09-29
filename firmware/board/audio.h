// Tonausgabe: DAC-Kanal 2 an PA5 (Kanal 1/PA4 ist VSYNC des Displays),
// Abtasttakt von TIM6, DMA1 Stream 6 im Kreis über einen Doppelpuffer.
// Mono, 16 Bit vorzeichenbehaftet; der DAC nutzt die oberen 12 Bit.
//
// Zwei Betriebsarten:
//   audio_init(rate, fill)     füllt die frei gewordene Pufferhälfte direkt
//                              im DMA-Interrupt (Inbetriebnahme, Testton)
//   audio_init_deferred(...)   der Interrupt meldet nur die freie Hälfte
//                              (wake, aus dem Interrupt); gefüllt wird in
//                              audio_service() außerhalb des Interrupts –
//                              im Spiel im Audio-Task, der sich mit ScummVM
//                              über Mutexe abstimmen kann
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*audio_fill_fn)(int16_t *samples, unsigned count);

#define AUDIO_HALF_SAMPLES 512u  // Samples je Pufferhälfte

// Priorität des DMA-Interrupts: unter SysTick/HAL-Zeitbasis und USB.
#define AUDIO_IRQ_PRIORITY 10u

void audio_init(unsigned rate, audio_fill_fn fill);
void audio_init_deferred(unsigned rate, void (*wake)(void));
// Füllt alle gemeldeten Hälften; aus dem Task nach wake() aufrufen.
void audio_service(audio_fill_fn fill);

unsigned audio_rate(void);  // tatsächliche Rate (ganzzahliger Timerteiler)
void audio_stop(void);
void audio_start(void);

// Anteil der CPU-Zeit im Füllen seit dem letzten Aufruf, in Promille.
unsigned audio_load_permille(void);
// Pufferunterläufe seit dem Start: Die Füllung kam zu spät (hörbar als Knacken).
unsigned audio_underruns(void);

#ifdef __cplusplus
}
#endif
