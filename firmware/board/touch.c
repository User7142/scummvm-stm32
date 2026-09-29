// Touch: STMPE811 an I²C3 (SCL PA8, SDA PC9, Adresse 0x82), Init-Sequenz
// aus dem ST-Komponententreiber stm32-stmpe811 (STMPE811_TS_Init).
#include "board.h"

#define STMPE811_ADDR 0x82

enum {
  REG_SYS_CTRL1 = 0x03,
  REG_SYS_CTRL2 = 0x04,
  REG_INT_STA = 0x0B,
  REG_IO_AF = 0x17,
  REG_ADC_CTRL1 = 0x20,
  REG_ADC_CTRL2 = 0x21,
  REG_TSC_CTRL = 0x40,
  REG_TSC_CFG = 0x41,
  REG_FIFO_TH = 0x4A,
  REG_FIFO_STA = 0x4B,
  REG_FIFO_SIZE = 0x4C,
  REG_TSC_FRACT_XYZ = 0x56,
  REG_TSC_I_DRIVE = 0x58,
  REG_TSC_DATA_NON_INC = 0xD7,
};

enum { FCT_ADC = 0x01, FCT_TS = 0x02, FCT_IO = 0x04 };
#define TOUCH_PINS 0xF0  // IO4–IO7: XU, YU, XD, YD

static I2C_HandleTypeDef i2c;

static void write_reg(uint8_t reg, uint8_t value) {
  if (HAL_I2C_Mem_Write(&i2c, STMPE811_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100) != HAL_OK) board_panic(4);
}

static void read_regs(uint8_t reg, uint8_t *buf, uint16_t n) {
  if (HAL_I2C_Mem_Read(&i2c, STMPE811_ADDR, reg, I2C_MEMADD_SIZE_8BIT, buf, n, 100) != HAL_OK) board_panic(4);
}

static uint8_t read_reg(uint8_t reg) {
  uint8_t v;
  read_regs(reg, &v, 1);
  return v;
}

// I²C-Bus freigeben. Wird der Controller mitten in einem Lesezugriff
// zurückgesetzt (Reset-Taste, Debugger, Sprung vom Lader ins Spiel), wartet
// der STMPE811 noch auf Takte für sein Byte und hält SDA auf Low – jeder neue
// Zugriff scheitert dann. Abhilfe nach I²C-Spezifikation (Abschnitt 3.1.16):
// SCL takten, bis SDA frei ist (höchstens 9 Takte), dann STOP senden.
static void bus_recover(void) {
  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_OUTPUT_OD;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);
  g.Pin = GPIO_PIN_8;
  HAL_GPIO_Init(GPIOA, &g);
  g.Pin = GPIO_PIN_9;
  HAL_GPIO_Init(GPIOC, &g);
  HAL_Delay(1);

  for (int i = 0; i < 9 && HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9) == GPIO_PIN_RESET; ++i) {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
    HAL_Delay(1);
  }
  // STOP: SDA steigt, während SCL high ist.
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);
  HAL_Delay(1);
}

void touch_init(void) {
  __HAL_RCC_I2C3_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  bus_recover();
  // Auch das I²C-Modul selbst kann aus dem Vorgängerprogramm noch „busy“ sein.
  __HAL_RCC_I2C3_FORCE_RESET();
  __HAL_RCC_I2C3_RELEASE_RESET();

  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_AF_OD;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF4_I2C3;
  g.Pin = GPIO_PIN_8;
  HAL_GPIO_Init(GPIOA, &g);
  g.Pin = GPIO_PIN_9;
  HAL_GPIO_Init(GPIOC, &g);

  i2c.Instance = I2C3;
  i2c.Init.ClockSpeed = 100000;
  i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
  i2c.Init.OwnAddress1 = 0;
  i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&i2c) != HAL_OK) board_panic(4);

  write_reg(REG_SYS_CTRL1, 0x02);  // Soft-Reset
  HAL_Delay(10);
  write_reg(REG_SYS_CTRL1, 0x00);
  HAL_Delay(2);

  uint8_t ctrl2 = read_reg(REG_SYS_CTRL2);
  ctrl2 &= ~FCT_IO;                                          // GPIO-Takt an
  write_reg(REG_SYS_CTRL2, ctrl2);
  write_reg(REG_IO_AF, read_reg(REG_IO_AF) & ~TOUCH_PINS);   // Touch-Pins an den TSC
  ctrl2 &= ~(FCT_TS | FCT_ADC);                              // TSC und ADC an
  write_reg(REG_SYS_CTRL2, ctrl2);

  write_reg(REG_ADC_CTRL1, 0x48);  // 80 Takte Abtastzeit, 12 Bit
  HAL_Delay(2);
  write_reg(REG_ADC_CTRL2, 0x01);  // ADC-Takt 3,25 MHz
  write_reg(REG_TSC_CFG, 0x9A);    // 4 Messungen mitteln, 500 µs Verzögerung/Einschwingzeit
  write_reg(REG_FIFO_TH, 0x01);
  write_reg(REG_FIFO_STA, 0x01);   // FIFO zurücksetzen
  write_reg(REG_FIFO_STA, 0x00);
  write_reg(REG_TSC_FRACT_XYZ, 0x01);
  write_reg(REG_TSC_I_DRIVE, 0x01);  // 50 mA
  write_reg(REG_TSC_CTRL, 0x73);     // Fenster-Tracking 127, nur X/Y, TSC an
  write_reg(REG_INT_STA, 0xFF);
  HAL_Delay(2);
}

bool touch_read(uint16_t *raw_x, uint16_t *raw_y) {
  if (!(read_reg(REG_TSC_CTRL) & 0x80) || read_reg(REG_FIFO_SIZE) == 0) return false;
  uint8_t d[3];
  read_regs(REG_TSC_DATA_NON_INC, d, 3);
  uint32_t xy = ((uint32_t)d[0] << 16) | ((uint32_t)d[1] << 8) | d[2];
  *raw_x = (xy >> 12) & 0x0FFF;
  *raw_y = xy & 0x0FFF;
  write_reg(REG_FIFO_STA, 0x01);  // FIFO leeren, damit der nächste Wert frisch ist
  write_reg(REG_FIFO_STA, 0x00);
  return true;
}

// Kalibrierung, am Board gemessen (2026-09-29): Tippen oben links ergab roh
// (3659, 426), unten rechts (346, 3710). Linear: X fällt mit 14,7 Rohschritten
// je Pixel ab ≈ 3791, Y steigt mit 11,0 je Pixel ab ≈ 360. Das deckt sich mit
// den Konstanten des ST-BSP (3800/15, 360/11); dessen Knick bei roh 3000
// zeigt die Messung nicht.
void touch_to_panel(uint16_t raw_x, uint16_t raw_y, int16_t *x, int16_t *y) {
  int px = (3791 - (int)raw_x) * 10 / 147;
  int py = ((int)raw_y - 360) * 10 / 110;
  *x = (int16_t)(px < 0 ? 0 : px >= (int)LCD_WIDTH ? (int)LCD_WIDTH - 1 : px);
  *y = (int16_t)(py < 0 ? 0 : py >= (int)LCD_HEIGHT ? (int)LCD_HEIGHT - 1 : py);
}
