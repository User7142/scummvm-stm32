#include "stm32-mixer.h"

#include "audio/mixer_intern.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "audio.h"
#include "rtos.h"

namespace {

Audio::MixerImpl *g_mixer = nullptr;
TaskHandle_t g_audioTask = nullptr;

// Stack im internen SRAM: Der Mixer ruft die OPL-Emulation, die allein 4 KB
// Zwischenpuffer auf dem Stack anlegt.
constexpr uint32_t kAudioStackWords = 3072;  // 12 KB
__attribute__((section(".ram_data"))) StackType_t g_audioStack[kAudioStackWords];
__attribute__((section(".ram_data"))) StaticTask_t g_audioTaskBuffer;

void fill(int16_t *samples, unsigned count) {
  g_mixer->mixCallback(reinterpret_cast<byte *>(samples), count * sizeof(int16_t));
}

// Aus dem DMA-Interrupt: frei gewordene Pufferhälfte melden.
void wake() {
  BaseType_t higherPriorityWoken = pdFALSE;
  vTaskNotifyGiveFromISR(g_audioTask, &higherPriorityWoken);
  portYIELD_FROM_ISR(higherPriorityWoken);
}

void audioTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    audio_service(fill);
  }
}

}  // namespace

Stm32MixerManager::~Stm32MixerManager() {
  audio_stop();
}

void Stm32MixerManager::init() {
  _mixer = new Audio::MixerImpl(kRate, false, AUDIO_HALF_SAMPLES);
  _mixer->setReady(true);
  g_mixer = _mixer;
  g_audioTask = xTaskCreateStatic(audioTask, "audio", kAudioStackWords, nullptr, RTOS_PRIORITY_AUDIO, g_audioStack,
                                  &g_audioTaskBuffer);
  audio_init_deferred(kRate, wake);
}

void Stm32MixerManager::suspendAudio() {
  audio_stop();
  _audioSuspended = true;
}

int Stm32MixerManager::resumeAudio() {
  if (!_audioSuspended) return -2;  // wie die übrigen Backends: war nicht angehalten
  audio_start();
  _audioSuspended = false;
  return 0;
}

Stm32Mutex::Stm32Mutex() : _handle(xSemaphoreCreateRecursiveMutex()) {
  configASSERT(_handle);
}

Stm32Mutex::~Stm32Mutex() {
  vSemaphoreDelete(static_cast<SemaphoreHandle_t>(_handle));
}

bool Stm32Mutex::lock() {
  return xSemaphoreTakeRecursive(static_cast<SemaphoreHandle_t>(_handle), portMAX_DELAY) == pdTRUE;
}

bool Stm32Mutex::unlock() {
  return xSemaphoreGiveRecursive(static_cast<SemaphoreHandle_t>(_handle)) == pdTRUE;
}
