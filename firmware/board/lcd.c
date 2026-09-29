// Display: ILI9341 im RGB-Modus am LTDC, Konfiguration über SPI5.
// Pins, Timings und die Init-Sequenz stammen aus dem ST-BSP
// (stm32f429i_discovery_lcd.c, stm32f429i_discovery.c, Komponente ili9341.c).
#include "board.h"

static LTDC_HandleTypeDef ltdc;
static SPI_HandleTypeDef spi;

// --- SPI5: Steuerkanal zum ILI9341 (CS an PC2, D/C „WRX“ an PD13) -------------

static void spi_init(void) {
  __HAL_RCC_SPI5_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;  // SCK, MISO, MOSI
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_PULLDOWN;
  g.Speed = GPIO_SPEED_FREQ_MEDIUM;
  g.Alternate = GPIO_AF5_SPI5;
  HAL_GPIO_Init(GPIOF, &g);

  g.Pin = GPIO_PIN_2;  // CS
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOC, &g);
  g.Pin = GPIO_PIN_13;  // WRX (Daten/Befehl)
  HAL_GPIO_Init(GPIOD, &g);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);

  spi.Instance = SPI5;
  spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;  // 84 MHz / 16 = 5,25 MHz
  spi.Init.Direction = SPI_DIRECTION_2LINES;
  spi.Init.CLKPhase = SPI_PHASE_1EDGE;
  spi.Init.CLKPolarity = SPI_POLARITY_LOW;
  spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  spi.Init.CRCPolynomial = 7;
  spi.Init.DataSize = SPI_DATASIZE_8BIT;
  spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
  spi.Init.NSS = SPI_NSS_SOFT;
  spi.Init.TIMode = SPI_TIMODE_DISABLE;
  spi.Init.Mode = SPI_MODE_MASTER;
  if (HAL_SPI_Init(&spi) != HAL_OK) board_panic(3);
}

static void ili_send(bool data, uint8_t value) {
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, data ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);
  if (HAL_SPI_Transmit(&spi, &value, 1, 100) != HAL_OK) board_panic(3);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);
}

// Befehl mit Parametern; Tabelle: Befehl, Anzahl, Parameter …
static void ili_command(uint8_t cmd, const uint8_t *params, unsigned count) {
  ili_send(false, cmd);
  for (unsigned i = 0; i < count; ++i) ili_send(true, params[i]);
}

#define ILI(cmd, ...)                                               \
  do {                                                              \
    static const uint8_t p[] = {__VA_ARGS__};                       \
    ili_command(cmd, p, sizeof(p));                                 \
  } while (0)

static void ili9341_init(void) {
  ILI(0xCA, 0xC3, 0x08, 0x50);
  ILI(0xCF, 0x00, 0xC1, 0x30);              // Power control B
  ILI(0xED, 0x64, 0x03, 0x12, 0x81);        // Power on sequence
  ILI(0xE8, 0x85, 0x00, 0x78);              // Driver timing A
  ILI(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);  // Power control A
  ILI(0xF7, 0x20);                          // Pump ratio
  ILI(0xEA, 0x00, 0x00);                    // Driver timing B
  ILI(0xB1, 0x00, 0x1B);                    // Frame rate
  ILI(0xB6, 0x0A, 0xA2);                    // Display function
  ILI(0xC0, 0x10);                          // Power 1
  ILI(0xC1, 0x10);                          // Power 2
  ILI(0xC5, 0x45, 0x15);                    // VCOM 1
  ILI(0xC7, 0x90);                          // VCOM 2
  ILI(0x36, 0xC8);                          // Memory access control
  ILI(0xF2, 0x00);                          // 3-Gamma aus
  ILI(0xB0, 0xC2);                          // RGB-Interface
  ILI(0xB6, 0x0A, 0xA7, 0x27, 0x04);        // Display function
  ILI(0x2A, 0x00, 0x00, 0x00, 0xEF);        // Spalten 0–239
  ILI(0x2B, 0x00, 0x00, 0x01, 0x3F);        // Zeilen 0–319
  ILI(0xF6, 0x01, 0x00, 0x06);              // Interface: RGB-Modus
  ili_command(0x2C, NULL, 0);               // GRAM
  HAL_Delay(200);
  ILI(0x26, 0x01);                          // Gamma
  ILI(0xE0, 0x0F, 0x29, 0x24, 0x0C, 0x0E, 0x09, 0x4E, 0x78, 0x3C, 0x09, 0x13, 0x05, 0x17, 0x11, 0x00);
  ILI(0xE1, 0x00, 0x16, 0x1B, 0x04, 0x11, 0x07, 0x31, 0x33, 0x42, 0x05, 0x0C, 0x0A, 0x28, 0x2F, 0x0F);
  ili_command(0x11, NULL, 0);               // Sleep out
  HAL_Delay(200);
  ili_command(0x29, NULL, 0);               // Display on
  ili_command(0x2C, NULL, 0);
}

// --- LTDC ----------------------------------------------------------------------

static void ltdc_pins(void) {
  __HAL_RCC_LTDC_CLK_ENABLE();
  __HAL_RCC_DMA2D_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF14_LTDC;
  g.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_11 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOA, &g);
  g.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
  HAL_GPIO_Init(GPIOB, &g);
  g.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_10;
  HAL_GPIO_Init(GPIOC, &g);
  g.Pin = GPIO_PIN_3 | GPIO_PIN_6;
  HAL_GPIO_Init(GPIOD, &g);
  g.Pin = GPIO_PIN_10;
  HAL_GPIO_Init(GPIOF, &g);
  g.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_11;
  HAL_GPIO_Init(GPIOG, &g);
  g.Alternate = GPIO_AF9_LTDC;
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1;
  HAL_GPIO_Init(GPIOB, &g);
  g.Pin = GPIO_PIN_10 | GPIO_PIN_12;
  HAL_GPIO_Init(GPIOG, &g);
}

// Schattenregister sofort übernehmen und warten, bis die Hardware es quittiert
// (IMR wird im Pixeltakt gelöscht).
static void reload_and_wait(void) {
  if (HAL_LTDC_Reload(&ltdc, LTDC_RELOAD_IMMEDIATE) != HAL_OK) board_panic(3);
  uint32_t start = HAL_GetTick();
  while (LTDC->SRCR & LTDC_SRCR_IMR) {
    if (HAL_GetTick() - start > 100) board_panic(3);
  }
}

// Bildspeicher von Ebene 0, Grundlage für lcd_place_layer0().
static uint8_t *layer0_fb;

void lcd_init(uint8_t *framebuffer, unsigned x0, unsigned width, uint16_t *overlay) {
  layer0_fb = framebuffer;

  // Pixeltakt: PLLSAI 192 MHz / R 4 / DIVR 8 = 6 MHz (wie im BSP).
  RCC_PeriphCLKInitTypeDef pclk = {0};
  pclk.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
  pclk.PLLSAI.PLLSAIN = 192;
  pclk.PLLSAI.PLLSAIR = 4;
  pclk.PLLSAIDivR = RCC_PLLSAIDIVR_8;
  if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) board_panic(3);

  ltdc_pins();
  ltdc.Instance = LTDC;
  // ILI9341-Timing: HSYNC 10, HBP 20, 240 aktiv, HFP 10; VSYNC 2, VBP 2, 320 aktiv, VFP 4
  ltdc.Init.HorizontalSync = 9;
  ltdc.Init.VerticalSync = 1;
  ltdc.Init.AccumulatedHBP = 29;
  ltdc.Init.AccumulatedVBP = 3;
  ltdc.Init.AccumulatedActiveW = 269;
  ltdc.Init.AccumulatedActiveH = 323;
  ltdc.Init.TotalWidth = 279;
  ltdc.Init.TotalHeigh = 327;
  ltdc.Init.Backcolor.Red = 0;
  ltdc.Init.Backcolor.Green = 0;
  ltdc.Init.Backcolor.Blue = 0;
  ltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  ltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  ltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  ltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  if (HAL_LTDC_Init(&ltdc) != HAL_OK) board_panic(3);

  spi_init();
  ili9341_init();

  // Eine Ebene im Format L8: ein Byte je Pixel, Farben aus der CLUT –
  // passt genau zu den 256-Farben-Bildern von SCUMM.
  LTDC_LayerCfgTypeDef layer = {0};
  layer.WindowX0 = x0;
  layer.WindowX1 = x0 + width;
  layer.WindowY0 = 0;
  layer.WindowY1 = LCD_HEIGHT;
  layer.PixelFormat = LTDC_PIXEL_FORMAT_L8;
  layer.Alpha = 255;
  layer.Alpha0 = 0;
  layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  layer.FBStartAdress = (uint32_t)framebuffer;
  layer.ImageWidth = width;
  layer.ImageHeight = LCD_HEIGHT;

  // Die Layer-Register sind Schattenregister, gelesen wird aber der aktive
  // Wert. Setzt man Bits per Lesen-Ändern-Schreiben (so macht es die HAL),
  // geht alles verloren, was seit dem letzten Reload nur im Schatten stand:
  // EnableCLUT hat so das Enable-Bit von ConfigLayer überschrieben (am Board
  // gemessen: LxCR = CLUTEN ohne LEN). Deshalb nach jedem Schritt ein Reload,
  // auf dessen Abschluss gewartet wird.
  if (HAL_LTDC_ConfigLayer_NoReload(&ltdc, &layer, 0) != HAL_OK) board_panic(3);
  reload_and_wait();
  if (HAL_LTDC_EnableCLUT_NoReload(&ltdc, 0) != HAL_OK) board_panic(3);
  reload_and_wait();

  if (overlay) {
    // Ebene 1: RGB565 für die ScummVM-Menüs, deckend über Ebene 0; aus, bis
    // lcd_show_overlay() sie braucht.
    LTDC_LayerCfgTypeDef top = layer;
    top.WindowX0 = 0;
    top.WindowX1 = LCD_WIDTH;
    top.ImageWidth = LCD_WIDTH;
    top.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    top.FBStartAdress = (uint32_t)overlay;
    // Eine abgeschaltete Ebene liefert ihre Default-Farbe (DCCR, hier Alpha 0)
    // weiter an die Mischstufe (RM0090, LTDC). Mit dem Faktor „konstantes
    // Alpha“ läge sie damit als deckendes Schwarz über Ebene 0 – am Board
    // gemessen. Pixel-Alpha × konstantes Alpha macht die Default-Farbe
    // durchsichtig; RGB565-Pixel haben Alpha 255, die Menüs bleiben deckend.
    top.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    top.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    if (HAL_LTDC_ConfigLayer_NoReload(&ltdc, &top, 1) != HAL_OK) board_panic(3);
    reload_and_wait();
    __HAL_LTDC_LAYER_DISABLE(&ltdc, 1);
    reload_and_wait();
  }
}

void lcd_show_overlay(bool on) {
  if (on) __HAL_LTDC_LAYER_ENABLE(&ltdc, 1);
  else __HAL_LTDC_LAYER_DISABLE(&ltdc, 1);
  // Umschalten in der Austastlücke, damit kein halbes Bild zu sehen ist.
  if (HAL_LTDC_Reload(&ltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK) board_panic(3);
}

void lcd_set_palette(const uint8_t *palette, unsigned first, unsigned count) {
  // Die CLUT darf nur in der Austastlücke (oder bei abgeschalteter Ebene)
  // beschrieben werden (RM0090, LTDC_LxCLUTWR); Schreiben im laufenden Bild
  // geht verloren. Deshalb auf den Beginn der vertikalen Austastlücke warten
  // (VDES fällt) – sie dauert 8 Zeilen ≈ 370 µs, 256 Einträge brauchen wenige µs.
  uint32_t start = HAL_GetTick();
  while (!(LTDC->CDSR & LTDC_CDSR_VDES)) {
    if (HAL_GetTick() - start > 100) board_panic(3);
  }
  while (LTDC->CDSR & LTDC_CDSR_VDES) {
    if (HAL_GetTick() - start > 100) board_panic(3);
  }
  for (unsigned i = 0; i < count && first + i < 256; ++i) {
    const uint8_t *c = palette + 3 * i;
    LTDC_Layer1->CLUTWR = ((first + i) << 24) | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2];
  }
}

void lcd_place_layer0(int x0, int y0, unsigned width, unsigned height) {
  // Sichtbarer Ausschnitt in Panelkoordinaten, auf das Panel beschnitten.
  int x1 = x0 + (int)width, y1 = y0 + (int)height;
  int vx0 = x0 < 0 ? 0 : x0, vx1 = x1 > (int)LCD_WIDTH ? (int)LCD_WIDTH : x1;
  int vy0 = y0 < 0 ? 0 : y0, vy1 = y1 > (int)LCD_HEIGHT ? (int)LCD_HEIGHT : y1;
  if (vx1 <= vx0 || vy1 <= vy0) board_panic(3);  // Bild läge ganz außerhalb
  const uint8_t *start = layer0_fb + (vy0 - y0) * (int)width + (vx0 - x0);

  // Ganze Register schreiben statt HAL-Lesen-Ändern-Schreiben (siehe lcd_init);
  // Aufbau wie in LTDC_SetConfig der HAL, Zeilenlänge in Byte (L8) + 3.
  const uint32_t hbp = (LTDC->BPCR & LTDC_BPCR_AHBP) >> 16, vbp = LTDC->BPCR & LTDC_BPCR_AVBP;
  LTDC_Layer1->WHPCR = ((vx1 + hbp) << 16) | (vx0 + hbp + 1);
  LTDC_Layer1->WVPCR = ((vy1 + vbp) << 16) | (vy0 + vbp + 1);
  LTDC_Layer1->CFBAR = (uint32_t)start;
  LTDC_Layer1->CFBLR = (width << 16) | (uint32_t)(vx1 - vx0 + 3);
  LTDC_Layer1->CFBLNR = (uint32_t)(vy1 - vy0);
  if (HAL_LTDC_Reload(&ltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK) board_panic(3);
}
