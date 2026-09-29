// Zeitbasis der HAL (HAL_GetTick, HAL_Delay) auf TIM7 statt SysTick: den
// SysTick braucht FreeRTOS als Systemtakt. Ersetzt die schwachen
// HAL_InitTick/HAL_SuspendTick/HAL_ResumeTick der HAL.
#include "board.h"

HAL_StatusTypeDef HAL_InitTick(uint32_t priority) {
  __HAL_RCC_TIM7_CLK_ENABLE();
  // APB1-Timertakt = 2 × PCLK1 (APB1-Teiler ≠ 1): 1 MHz Zählertakt, 1 kHz Überlauf.
  const uint32_t timer_clock = HAL_RCC_GetPCLK1Freq() * 2u;
  TIM7->CR1 = 0;
  TIM7->PSC = timer_clock / 1000000u - 1u;
  TIM7->ARR = 1000u - 1u;
  TIM7->EGR = TIM_EGR_UG;
  TIM7->SR = 0;
  TIM7->DIER = TIM_DIER_UIE;
  HAL_NVIC_SetPriority(TIM7_IRQn, priority, 0);
  HAL_NVIC_EnableIRQ(TIM7_IRQn);
  TIM7->CR1 = TIM_CR1_CEN;
  uwTickPrio = priority;
  return HAL_OK;
}

void HAL_SuspendTick(void) {
  TIM7->DIER &= ~TIM_DIER_UIE;
}

void HAL_ResumeTick(void) {
  TIM7->DIER |= TIM_DIER_UIE;
}

void TIM7_IRQHandler(void) {
  TIM7->SR = ~TIM_SR_UIF;
  HAL_IncTick();
}
