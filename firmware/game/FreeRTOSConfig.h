// FreeRTOS für das Spielprogramm: zwei Tasks (ScummVM, Tonausgabe).
//
// ScummVM erwartet, dass der Mixer in einem eigenen Faden läuft und jeder
// Mutex nur die Fäden aufhält, die ihn wirklich brauchen. Ohne RTOS sperrte
// jeder Mutex den Ton-Interrupt; hielt ScummVM beim Laden einer Ressource
// den Ressourcen-Mutex 400 ms, lief der Tonpuffer leer (Knacken).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t SystemCoreClock;
void rtos_assert_failed(const char *file, int line);
#ifdef __cplusplus
}
#endif

#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      1000
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  0
#define configMAX_PRIORITIES                    4
#define configMINIMAL_STACK_SIZE                128
#define configMAX_TASK_NAME_LEN                 8
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_TIMERS                        0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configCHECK_FOR_STACK_OVERFLOW          2

// Tasks mit statischem Stack im internen SRAM; Mutexe dynamisch über
// heap_3 (newlib-malloc im SDRAM).
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1

// Cortex-M4: 4 Prioritätsbits. Interrupts mit Priorität 0–4 (SysTick der
// HAL auf TIM7) werden vom Kern nie gesperrt und dürfen keine FreeRTOS-
// Funktionen rufen; der DMA-Interrupt der Tonausgabe liegt darunter (10).
#define configPRIO_BITS                                  4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY          15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5
#define configKERNEL_INTERRUPT_PRIORITY      (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskGetStackHighWaterMark     1

#define configASSERT(x)                         \
  do {                                          \
    if (!(x)) rtos_assert_failed(__FILE__, __LINE__); \
  } while (0)

// Handler des Ports unter den Namen der Vektortabelle.
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
