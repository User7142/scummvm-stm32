#include "audio.h"

#include "board.h"

static uint16_t buffer[2 * AUDIO_HALF_SAMPLES];
static audio_fill_fn isr_fill;       // Betriebsart „im Interrupt füllen“
static void (*wake_fn)(void);        // Betriebsart „Task wecken“
static volatile uint32_t pending;    // Bit 0/1: Hälfte 0/1 wartet aufs Füllen
static unsigned actual_rate;
static volatile uint32_t busy_cycles;
static uint32_t load_since;
static volatile unsigned underruns;

static void fill_half(uint16_t *half, audio_fill_fn fill) {
  const uint32_t start = DWT->CYCCNT;
  fill((int16_t *)half, AUDIO_HALF_SAMPLES);
  // DHR12L2 erwartet vorzeichenlose Werte, linksbündig: Vorzeichenbit kippen.
  for (unsigned i = 0; i < AUDIO_HALF_SAMPLES; ++i) half[i] ^= 0x8000u;
  busy_cycles += DWT->CYCCNT - start;

  // Liest der DMA schon wieder aus der Hälfte, die gerade erst fertig wurde,
  // kam die Füllung zu spät.
  const unsigned pos = 2u * AUDIO_HALF_SAMPLES - DMA1_Stream6->NDTR;
  const bool in_filled_half = (half == buffer) ? pos < AUDIO_HALF_SAMPLES : pos >= AUDIO_HALF_SAMPLES;
  if (in_filled_half) underruns++;
}

static void half_free(unsigned index) {
  if (isr_fill) {
    fill_half(buffer + index * AUDIO_HALF_SAMPLES, isr_fill);
  } else {
    // Ist die Hälfte noch vom letzten Mal offen, hat der Task sie nicht
    // rechtzeitig gefüllt: Unterlauf.
    if (pending & (1u << index)) underruns++;
    pending |= 1u << index;
    wake_fn();
  }
}

void DMA1_Stream6_IRQHandler(void) {
  const uint32_t flags = DMA1->HISR;
  if (flags & DMA_HISR_HTIF6) {
    DMA1->HIFCR = DMA_HIFCR_CHTIF6;
    half_free(0);
  }
  if (flags & DMA_HISR_TCIF6) {
    DMA1->HIFCR = DMA_HIFCR_CTCIF6;
    half_free(1);
  }
  if (flags & DMA_HISR_TEIF6) {
    DMA1->HIFCR = DMA_HIFCR_CTEIF6;
    board_panic(2);  // Busfehler der DMA: Adresse des Puffers ungültig
  }
}

void audio_service(audio_fill_fn fill) {
  for (unsigned index = 0; index < 2; ++index) {
    if (!(pending & (1u << index))) continue;
    fill_half(buffer + index * AUDIO_HALF_SAMPLES, fill);
    __disable_irq();
    pending &= ~(1u << index);
    __enable_irq();
  }
}

static void setup(unsigned rate) {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_DAC_CLK_ENABLE();
  __HAL_RCC_TIM6_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  GPIO_InitTypeDef pin = {0};
  pin.Pin = GPIO_PIN_5;
  pin.Mode = GPIO_MODE_ANALOG;
  pin.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &pin);

  // Zyklenzähler für die Lastmessung.
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  load_since = DWT->CYCCNT;

  // TIM6 am APB1-Timertakt (APB1 /4 → Timer ×2 = HCLK/2), Update → TRGO.
  const uint32_t timer_clock = HAL_RCC_GetPCLK1Freq() * 2u;
  const uint32_t period = (timer_clock + rate / 2u) / rate;
  actual_rate = timer_clock / period;
  TIM6->CR1 = 0;
  TIM6->PSC = 0;
  TIM6->ARR = period - 1u;
  TIM6->CR2 = TIM_CR2_MMS_1;  // MMS = 010: Update als Trigger
  TIM6->EGR = TIM_EGR_UG;

  // DAC-Kanal 2: Ausgangspuffer an, Trigger TIM6 (TSEL2 = 000), DMA an.
  DAC->CR = (DAC->CR & 0x0000FFFFu) | DAC_CR_EN2 | DAC_CR_TEN2 | DAC_CR_DMAEN2;
  DAC->DHR12L2 = 0x8000u;  // Mitte, bis die ersten Samples kommen

  for (unsigned i = 0; i < 2 * AUDIO_HALF_SAMPLES; ++i) buffer[i] = 0x8000u;

  // DMA1 Stream 6, Kanal 7 = DAC2: Speicher → Peripherie, 16 Bit, Kreis.
  DMA1_Stream6->CR = 0;
  while (DMA1_Stream6->CR & DMA_SxCR_EN) {}
  DMA1->HIFCR = DMA_HIFCR_CFEIF6 | DMA_HIFCR_CDMEIF6 | DMA_HIFCR_CTEIF6 | DMA_HIFCR_CHTIF6 | DMA_HIFCR_CTCIF6;
  DMA1_Stream6->PAR = (uint32_t)&DAC->DHR12L2;
  DMA1_Stream6->M0AR = (uint32_t)buffer;
  DMA1_Stream6->NDTR = 2u * AUDIO_HALF_SAMPLES;
  DMA1_Stream6->FCR = 0;  // Direktmodus
  DMA1_Stream6->CR = (7u << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL_1 | DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 |
                     DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_DIR_0 | DMA_SxCR_TCIE | DMA_SxCR_HTIE |
                     DMA_SxCR_TEIE;

  HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, AUDIO_IRQ_PRIORITY, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
  audio_start();
}

void audio_init(unsigned rate, audio_fill_fn fill) {
  isr_fill = fill;
  setup(rate);
}

void audio_init_deferred(unsigned rate, void (*wake)(void)) {
  wake_fn = wake;
  setup(rate);
}

unsigned audio_rate(void) {
  return actual_rate;
}

void audio_start(void) {
  DMA1_Stream6->CR |= DMA_SxCR_EN;
  TIM6->CR1 |= TIM_CR1_CEN;
}

void audio_stop(void) {
  TIM6->CR1 &= ~TIM_CR1_CEN;
}

unsigned audio_load_permille(void) {
  const uint32_t now = DWT->CYCCNT;
  const uint32_t total = now - load_since;
  __disable_irq();
  const uint32_t busy = busy_cycles;
  busy_cycles = 0;
  __enable_irq();
  load_since = now;
  return total ? (unsigned)((uint64_t)busy * 1000u / total) : 0u;
}

unsigned audio_underruns(void) {
  return underruns;
}
