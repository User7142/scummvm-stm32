#include "board.h"

static void clock_init(void) {
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  // HSE 8 MHz → PLL: /8 · 336 = VCO 336 MHz; /2 = 168 MHz SYSCLK; /7 = 48 MHz USB.
  RCC_OscInitTypeDef osc = {0};
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 8;
  osc.PLL.PLLN = 336;
  osc.PLL.PLLP = RCC_PLLP_DIV2;
  osc.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) board_panic(1);

  RCC_ClkInitTypeDef clk = {0};
  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;   // 168 MHz
  clk.APB1CLKDivider = RCC_HCLK_DIV4;    // 42 MHz
  clk.APB2CLKDivider = RCC_HCLK_DIV2;    // 84 MHz
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) board_panic(1);

  __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
  __HAL_FLASH_DATA_CACHE_ENABLE();
  __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
}

static void led_init(void) {
  __HAL_RCC_GPIOG_CLK_ENABLE();
  GPIO_InitTypeDef led = {0};
  led.Pin = GPIO_PIN_13 | GPIO_PIN_14;  // LD3 grün, LD4 rot
  led.Mode = GPIO_MODE_OUTPUT_PP;
  led.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &led);
}

void board_init(void) {
  HAL_Init();
  led_init();  // vor dem Takt, damit auch ein Fehler dort sichtbar wird
  clock_init();
}

void board_init_runtime(void) {
  // HAL_Init stellt SysTick auf den aktuellen Takt ein; der PLL läuft schon.
  SystemCoreClockUpdate();
  HAL_Init();
  led_init();
}

void board_led(int which, bool on) {
  HAL_GPIO_WritePin(GPIOG, which == LED_GREEN ? GPIO_PIN_13 : GPIO_PIN_14, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void button_init(void) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef pin = {0};
  pin.Pin = GPIO_PIN_0;
  pin.Mode = GPIO_MODE_INPUT;
  pin.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &pin);
}

bool button_pressed(void) {
  return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET;
}

_Noreturn void board_panic(int code) {
  __disable_irq();
  board_led(LED_GREEN, false);
  for (;;) {
    // Ohne SysTick (Interrupts aus) eine grobe Busy-Wait-Pause.
    for (int i = 0; i < code; ++i) {
      board_led(LED_RED, true);
      for (volatile uint32_t n = 0; n < 1500000; ++n) {}
      board_led(LED_RED, false);
      for (volatile uint32_t n = 0; n < 1500000; ++n) {}
    }
    for (volatile uint32_t n = 0; n < 6000000; ++n) {}
  }
}

// --- Interrupts ---------------------------------------------------------------

// Schwach: Das Spielprogramm überlässt den SysTick FreeRTOS und führt die
// HAL-Zeitbasis auf TIM7 (game/timebase.c).
__attribute__((weak)) void SysTick_Handler(void) {
  HAL_IncTick();
}

// Fehlerbild für den Debugger (Symbol board_fault): gesicherter Rahmen des
// unterbrochenen Codes, EXC_RETURN, Stackzeiger und die Fehlerregister.
volatile struct board_fault board_fault;

// Aufruf aus den Fault-Handlern mit dem Stackzeiger, auf dem die Hardware den
// Rahmen abgelegt hat, und dem EXC_RETURN-Wert aus LR.
__attribute__((used)) BOARD_NORETURN void fault_report(const uint32_t *frame, uint32_t exc_return, int code) {
  board_fault.magic = BOARD_FAULT_MAGIC;
  board_fault.code = (uint32_t)code;
  board_fault.exc_return = exc_return;
  board_fault.sp = (uint32_t)frame;
  for (int i = 0; i < 8; ++i) board_fault.frame[i] = frame[i];  // r0–r3, r12, lr, pc, xpsr
  board_fault.cfsr = SCB->CFSR;
  board_fault.hfsr = SCB->HFSR;
  board_fault.mmfar = SCB->MMFAR;
  board_fault.bfar = SCB->BFAR;
  board_panic(code);
}

// Welcher Stack den Rahmen trägt, steht in Bit 2 von EXC_RETURN.
#define FAULT_HANDLER(name, code)                         \
  __attribute__((naked)) void name(void) {                \
    __asm volatile("tst lr, #4\n"                         \
                   "ite eq\n"                             \
                   "mrseq r0, msp\n"                      \
                   "mrsne r0, psp\n"                      \
                   "mov r1, lr\n"                         \
                   "movs r2, #" #code "\n"                \
                   "b fault_report\n");                   \
  }

FAULT_HANDLER(HardFault_Handler, 9)
FAULT_HANDLER(MemManage_Handler, 8)
FAULT_HANDLER(BusFault_Handler, 7)
FAULT_HANDLER(UsageFault_Handler, 6)
