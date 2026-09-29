// ScummVM-Mixer auf dem DAC des Boards (board/audio.h), gemischt in einem
// eigenen FreeRTOS-Task – so wie ScummVM es erwartet: Der Mixer läuft
// nebenläufig und stimmt sich mit dem Spiel über Mutexe ab.
#pragma once

#include "backends/mixer/mixer.h"
#include "common/mutex.h"

class Stm32MixerManager : public MixerManager {
public:
  // Mono genügt: AdLib-Musik und die Effekte von SCUMM v5 sind mono.
  // 11025 Hz: Die OPL-Emulation rechnet mit der Ausgaberate, ihr Zustand liegt
  // auf dem Heap im SDRAM. Bei 22050 Hz brauchte sie am Board rund 70 % der
  // CPU, bei 11025 Hz etwa die Hälfte.
  static constexpr unsigned kRate = 11025;

  ~Stm32MixerManager() override;
  void init() override;
  void suspendAudio() override;
  int resumeAudio() override;
};

// ScummVM-Mutex als rekursiver FreeRTOS-Mutex (mit Prioritätsvererbung: hält
// das Spiel einen Mutex, den der Audio-Task braucht, läuft das Spiel mit
// dessen Priorität, bis es ihn freigibt).
class Stm32Mutex final : public Common::MutexInternal {
public:
  Stm32Mutex();
  ~Stm32Mutex() override;
  bool lock() override;
  bool unlock() override;

private:
  void *_handle;  // SemaphoreHandle_t
};
