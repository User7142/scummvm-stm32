// SDRAM IS42S16400J an FMC Bank 2 (0xD0000000), Werte aus dem ST-BSP
// (stm32f429i_discovery_sdram.c): 16 Bit, 4 Bänke, 12 Zeilen-/8 Spaltenbits,
// CAS 3, SDCLK = HCLK/2. Die BSP-Timings sind für 90 MHz SDCLK gerechnet;
// bei 84 MHz (168/2) sind sie entsprechend großzügiger.
#include "board.h"

static SDRAM_HandleTypeDef sdram;

static void sdram_pins(void) {
  __HAL_RCC_FMC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_AF_PP;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Pull = GPIO_NOPULL;
  g.Alternate = GPIO_AF12_FMC;

  g.Pin = GPIO_PIN_5 | GPIO_PIN_6;
  HAL_GPIO_Init(GPIOB, &g);
  g.Pin = GPIO_PIN_0;
  HAL_GPIO_Init(GPIOC, &g);
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOD, &g);
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
          GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOE, &g);
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 |
          GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOF, &g);
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOG, &g);
}

static void sdram_command(uint32_t mode, uint32_t refresh, uint32_t modereg) {
  FMC_SDRAM_CommandTypeDef cmd = {0};
  cmd.CommandMode = mode;
  cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
  cmd.AutoRefreshNumber = refresh;
  cmd.ModeRegisterDefinition = modereg;
  if (HAL_SDRAM_SendCommand(&sdram, &cmd, 0xFFFF) != HAL_OK) board_panic(2);
}

void sdram_init(void) {
  sdram_pins();

  FMC_SDRAM_TimingTypeDef t = {0};
  t.LoadToActiveDelay = 2;     // TMRD
  t.ExitSelfRefreshDelay = 7;  // TXSR ≥ 70 ns
  t.SelfRefreshTime = 4;       // TRAS ≥ 42 ns
  t.RowCycleDelay = 7;         // TRC ≥ 70 ns
  t.WriteRecoveryTime = 2;     // TWR
  t.RPDelay = 2;               // TRP ≥ 20 ns
  t.RCDDelay = 2;              // TRCD ≥ 20 ns

  sdram.Instance = FMC_SDRAM_DEVICE;
  sdram.Init.SDBank = FMC_SDRAM_BANK2;
  sdram.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
  sdram.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
  sdram.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  sdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  sdram.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  sdram.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  sdram.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  // Lese-Burst: Der Controller liest während der CAS-Latenz die Folgeworte
  // in seinen FIFO vor. Das Spielprogramm läuft aus dem SDRAM (Code und
  // Daten ungecacht); am Board gemessen spart das rund 7 % der Rechenzeit
  // der Musik-Emulation. RPIPE bleibt bei 1 Takt (Vorgabe des BSP für 84 MHz).
  sdram.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
  sdram.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;
  if (HAL_SDRAM_Init(&sdram, &t) != HAL_OK) board_panic(2);

  // JEDEC-Startsequenz: Takt an, alle Bänke vorladen, 4× Auto-Refresh,
  // Mode-Register (Burst 1, sequentiell, CAS 3, Einzelschreiben).
  sdram_command(FMC_SDRAM_CMD_CLK_ENABLE, 1, 0);
  HAL_Delay(1);
  sdram_command(FMC_SDRAM_CMD_PALL, 1, 0);
  sdram_command(FMC_SDRAM_CMD_AUTOREFRESH_MODE, 4, 0);
  sdram_command(FMC_SDRAM_CMD_LOAD_MODE, 1, 0x0000 | 0x0000 | 0x0030 | 0x0000 | 0x0200);
  // Refresh: 64 ms / 4096 Zeilen = 15,6 µs · 84 MHz − 20 ≈ 1292. Das BSP nimmt
  // 1386 (für 90 MHz); bei 84 MHz ergibt das 16,5 µs – zu langsam, daher neu
  // gerechnet.
  if (HAL_SDRAM_ProgramRefreshRate(&sdram, 1292) != HAL_OK) board_panic(2);

  // SDRAM als normalen, ausführbaren Speicher kennzeichnen (siehe board.h).
  // Alle übrigen Adressen behalten die Standard-Speicherkarte.
  HAL_MPU_Disable();
  MPU_Region_InitTypeDef r = {0};
  r.Enable = MPU_REGION_ENABLE;
  r.Number = MPU_REGION_NUMBER0;
  r.BaseAddress = (uint32_t)SDRAM_BASE;
  r.Size = MPU_REGION_SIZE_8MB;
  r.AccessPermission = MPU_REGION_FULL_ACCESS;
  r.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  r.TypeExtField = MPU_TEX_LEVEL0;
  r.IsCacheable = MPU_ACCESS_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  r.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  r.SubRegionDisable = 0;
  HAL_MPU_ConfigRegion(&r);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
