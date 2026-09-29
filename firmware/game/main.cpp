// Spielprogramm: ScummVM mit der SCUMM-Engine, läuft aus dem SDRAM.
//
// Der Lader (Flash) hat Takt, SDRAM und MPU eingerichtet, dieses Programm
// von MONKEY.BIN an 0xD0040000 geladen und seinen Reset_Handler angesprungen.
// Hier nur noch die Peripherie, die das Spiel selbst nutzt; danach laufen
// ScummVM und die Tonausgabe als FreeRTOS-Tasks.
#include <string.h>

#include "base/main.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "rtos.h"
#include "runtime.h"
#include "stm32-system.h"
#include "storage.h"

extern "C" uint8_t __fb_l8[];
extern "C" uint16_t __fb_rgb565[];

namespace {

// Stack des ScummVM-Tasks im internen SRAM (schnell; SDRAM ist ungecacht).
constexpr uint32_t kGameStackWords = 24576;  // 96 KB
__attribute__((section(".ram_data"))) StackType_t g_gameStack[kGameStackWords];
__attribute__((section(".ram_data"))) StaticTask_t g_gameTaskBuffer;

void gameTask(void *) {
  g_system = stm32_system_create();
  // Spiel liegt im Hauptverzeichnis des Sticks; ScummVM erkennt es selbst.
  char arg0[] = "scummvm", arg1[] = "--path=/", arg2[] = "--auto-detect";
  char *argv[] = {arg0, arg1, arg2, nullptr};
  const int res = scummvm_main(3, argv);
  g_system->destroy();

  log_str(res == 0 ? "ScummVM beendet\n" : "ScummVM mit Fehler beendet\n");
  NVIC_SystemReset();
}

}  // namespace

int main() {
  runtime_relocate_vectors();
  board_init_runtime();
  __enable_irq();
  board_led(LED_GREEN, true);
  log_str("MONKEY.BIN gestartet\n");

  // Ebene 0 zunächst leer (Platzierung legt der Grafikmanager fest), Ebene 1
  // für die Menüs.
  memset(__fb_l8, 0, LCD_WIDTH * LCD_HEIGHT);
  memset(__fb_rgb565, 0, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t));
  lcd_init(__fb_l8, 0, LCD_WIDTH, __fb_rgb565);
  touch_init();
  button_init();
  storage_init();
  if (!storage_wait(5000)) {
    log_str("Kein USB-Stick an USB USER\n");
    board_panic(4);
  }

  // Erst jetzt FreeRTOS: Bis zum Start des Schedulers sperrt der Kern nach
  // dem ersten API-Aufruf die Interrupts ab Priorität 5 (USB, Ton), die
  // Initialisierung oben braucht den USB-Interrupt aber.
  xTaskCreateStatic(gameTask, "game", kGameStackWords, nullptr, RTOS_PRIORITY_GAME, g_gameStack, &g_gameTaskBuffer);
  vTaskStartScheduler();
  board_panic(1);  // kehrt nur zurück, wenn der Scheduler nicht starten konnte
}
