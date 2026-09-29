// Aufteilung des Spielprogramms auf FreeRTOS-Tasks.
#pragma once

// Der Audio-Task muss das Spiel jederzeit unterbrechen können, sonst läuft
// der Tonpuffer leer, während ScummVM rechnet oder vom Stick lädt.
#define RTOS_PRIORITY_GAME  1
#define RTOS_PRIORITY_AUDIO 3
