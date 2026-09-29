// OSystem für das STM32F429I-Discovery: Grafik über den LTDC, Eingabe per
// Touch und USER-Taste, Dateien per FatFs vom USB-Stick, Ton über den DAC.
#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "stm32-system.h"

#include "backends/events/default/default-events.h"
#include "backends/modular-backend.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/queue.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "audio.h"
#include "board.h"
#include "fatfs-fs.h"
#include "ltdc-graphics.h"
#include "stm32-mixer.h"
#include "runtime.h"
#include "storage.h"

extern "C" uint8_t __fb_l8[];
extern "C" uint16_t __fb_rgb565[];

namespace {

// USER-Taste: kurz = Escape (Zwischensequenz überspringen), lang = ScummVM-Menü.
constexpr uint32 kLongPressMs = 800;

class OSystem_STM32 : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
public:
  OSystem_STM32() { _fsFactory = new FatFsFilesystemFactory(); }

  void initBackend() override {
    _timerManager = new DefaultTimerManager();
    _eventManager = new DefaultEventManager(this);
    _savefileManager = new DefaultSaveFileManager(Common::Path("/SAVES"));
    _graphicsManager = new LtdcGraphicsManager(__fb_l8, __fb_rgb565);
    // AdLib-Emulation: DOSBox (DBOPL) statt MAME. Deren Tabellen sind statisch
    // und liegen im internen SRAM (stm32f429zi_sdram.ld); MAME legt seine per
    // new im SDRAM an und brauchte so am Board 68 % der CPU. Als Standard
    // registriert, im ScummVM-Menü weiterhin umstellbar.
    ConfMan.registerDefault("opl_driver", "db");
    _mixerManager = new Stm32MixerManager();
    _mixerManager->init();
    BaseBackend::initBackend();
  }

  bool pollEvent(Common::Event &event) override {
    service();
    if (_queue.empty()) {
      pollTouch();
      pollButton();
    }
    if (_queue.empty()) return false;
    event = _queue.pop();
    return true;
  }

  Common::MutexInternal *createMutex() override { return new Stm32Mutex(); }

  uint32 getMillis(bool skipRecord) override { return HAL_GetTick(); }

  // Wartezeit: USB-Host und Timer weiter bedienen, dazwischen die CPU abgeben.
  void delayMillis(uint msecs) override {
    const uint32 start = HAL_GetTick();
    for (;;) {
      service();
      if (HAL_GetTick() - start >= msecs) break;
      vTaskDelay(1);
    }
  }

  // Das Board hat keine Uhr mit Batterie (kein 32-kHz-Quarz bestückt); ein
  // fester Zeitpunkt, damit Spielstände überhaupt ein Datum tragen.
  void getTimeAndDate(TimeDate &td, bool skipRecord) const override {
    td.tm_sec = td.tm_min = td.tm_hour = 0;
    td.tm_mday = 1;
    td.tm_mon = 0;
    td.tm_year = 2026 - 1900;
    td.tm_wday = 4;
  }

  // Ein Beenden gibt es auf dem Board nicht: neu starten, der Lader lädt
  // das Spiel wieder.
  void quit() override { NVIC_SystemReset(); }

  void logMessage(LogMessageType::Type type, const char *message) override { log_str(message); }

  Common::Path getDefaultConfigFileName() override { return Common::Path("/SCUMMVM.INI"); }

  void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override {}

private:
  LtdcGraphicsManager *graphics() { return static_cast<LtdcGraphicsManager *>(_graphicsManager); }

  void service() {
    storage_poll();
    static_cast<DefaultTimerManager *>(_timerManager)->checkTimers();
    logStats();
  }

  // Alle 10 s Tonlast, Unterläufe, freier Stack des Spiel-Tasks und Heap ins
  // Log (tools/read-log.sh).
  void logStats() {
    const uint32 now = HAL_GetTick();
    if (now - _statsSince < 10000) return;
    _statsSince = now;
    char line[96];
    const unsigned load = audio_load_permille();
    snprintf(line, sizeof(line), "Ton %u,%u %% CPU, %u Unterlaeufe, Stack frei %u KB, Heap %u KB\n", load / 10,
             load % 10, audio_underruns(), (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * 4 / 1024),
             (unsigned)(runtime_heap_used() / 1024));
    log_str(line);
  }

  void push(Common::EventType type, const Common::Point &pos) {
    Common::Event e;
    e.type = type;
    e.mouse = pos;
    _queue.push(e);
  }

  void pushKey(Common::KeyCode code, uint16 ascii) {
    Common::Event e;
    e.kbd = Common::KeyState(code, ascii);
    e.type = Common::EVENT_KEYDOWN;
    _queue.push(e);
    e.type = Common::EVENT_KEYUP;
    _queue.push(e);
  }

  // Berührung = linke Maustaste: Aufsetzen bewegt den Zeiger dorthin und
  // drückt, Ziehen bewegt, Abheben lässt los.
  void pollTouch() {
    uint16_t rx, ry;
    const bool down = touch_read(&rx, &ry);
    if (down) {
      int16_t px, py;
      touch_to_panel(rx, ry, &px, &py);
      const Common::Point pos = graphics()->fromPanel(px, py);
      if (!_touching || pos != _touchPos) {
        graphics()->setMousePos(pos);
        push(Common::EVENT_MOUSEMOVE, pos);
      }
      if (!_touching) push(Common::EVENT_LBUTTONDOWN, pos);
      _touchPos = pos;
    } else if (_touching) {
      push(Common::EVENT_LBUTTONUP, _touchPos);
    }
    _touching = down;
  }

  void pollButton() {
    const bool down = button_pressed();
    const uint32 now = HAL_GetTick();
    if (down && !_buttonDown) {
      _buttonSince = now;
      _buttonLong = false;
    } else if (down && !_buttonLong && now - _buttonSince >= kLongPressMs) {
      _buttonLong = true;  // Menü schon beim Halten öffnen, nicht erst beim Loslassen
      Common::Event e;
      e.type = Common::EVENT_MAINMENU;
      _queue.push(e);
    } else if (!down && _buttonDown && !_buttonLong) {
      pushKey(Common::KEYCODE_ESCAPE, Common::ASCII_ESCAPE);
    }
    _buttonDown = down;
  }

  Common::Queue<Common::Event> _queue;
  bool _touching = false;
  Common::Point _touchPos;
  bool _buttonDown = false, _buttonLong = false;
  uint32 _buttonSince = 0;
  uint32 _statsSince = 0;
};

}  // namespace

OSystem *stm32_system_create() {
  return new OSystem_STM32();
}
