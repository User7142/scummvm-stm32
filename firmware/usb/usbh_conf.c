// Anbindung der ST-USB-Host-Bibliothek an den HCD-Treiber der HAL.
//
// Der Port „USB USER“ des STM32F429I-Discovery hängt am OTG_HS-Controller,
// betrieben mit dem internen Full-Speed-PHY: DM/DP an PB14/PB15 (AF12),
// VBUS-Schalter (STMPS2151, Enable aktiv low) an PC4.
#include "usbh_core.h"

#include "board.h"

static HCD_HandleTypeDef hcd;

// --- statischer Speicher für den Klassentreiber --------------------------------

void *usbh_static_malloc(uint32_t size) {
  static uint32_t block[256];  // 1 KB, reicht für MSC_HandleTypeDef
  static bool used;
  if (used || size > sizeof(block)) board_panic(6);
  used = true;
  return block;
}

void usbh_static_free(void *p) {
  // Der eine Block bleibt reserviert; ein erneutes Einstecken nutzt ihn wieder.
  (void)p;
}

// --- HAL-HCD: Pins, Takt, Interrupt ---------------------------------------------

void HAL_HCD_MspInit(HCD_HandleTypeDef *h) {
  (void)h;
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  GPIO_InitTypeDef g = {0};
  g.Pin = GPIO_PIN_14 | GPIO_PIN_15;
  g.Mode = GPIO_MODE_AF_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  g.Alternate = GPIO_AF12_OTG_HS_FS;
  HAL_GPIO_Init(GPIOB, &g);

  // VBUS aus, bis der Host-Stack es einschaltet
  g.Pin = GPIO_PIN_4;
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Alternate = 0;
  HAL_GPIO_Init(GPIOC, &g);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_SET);

  __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
  HAL_NVIC_SetPriority(OTG_HS_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}

void OTG_HS_IRQHandler(void) {
  HAL_HCD_IRQHandler(&hcd);
}

void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *h) { USBH_LL_IncTimer(h->pData); }
void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *h) { USBH_LL_Connect(h->pData); }
void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *h) { USBH_LL_Disconnect(h->pData); }
void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *h) { USBH_LL_PortEnabled(h->pData); }
void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *h) { USBH_LL_PortDisabled(h->pData); }
void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef *h, uint8_t chnum, HCD_URBStateTypeDef urb) {
  // Ohne Betriebssystem fragt der Stack den URB-Zustand selbst ab.
  (void)h;
  (void)chnum;
  (void)urb;
}

static USBH_StatusTypeDef status(HAL_StatusTypeDef s) {
  switch (s) {
    case HAL_OK: return USBH_OK;
    case HAL_BUSY: return USBH_BUSY;
    default: return USBH_FAIL;
  }
}

// --- USBH_LL_* ---------------------------------------------------------------------

USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *phost) {
  hcd.pData = phost;
  phost->pData = &hcd;
  hcd.Instance = USB_OTG_HS;
  hcd.Init.Host_channels = 12;
  hcd.Init.speed = HCD_SPEED_FULL;
  hcd.Init.dma_enable = 0;
  hcd.Init.phy_itface = HCD_PHY_EMBEDDED;
  hcd.Init.Sof_enable = 0;
  hcd.Init.low_power_enable = 0;
  hcd.Init.vbus_sensing_enable = 0;
  hcd.Init.use_external_vbus = 0;
  if (HAL_HCD_Init(&hcd) != HAL_OK) board_panic(6);
  USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hcd));
  return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *phost) { return status(HAL_HCD_DeInit(phost->pData)); }
USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *phost) { return status(HAL_HCD_Start(phost->pData)); }
USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *phost) { return status(HAL_HCD_Stop(phost->pData)); }

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *phost) {
  switch (HAL_HCD_GetCurrentSpeed(phost->pData)) {
    case 0: return USBH_SPEED_HIGH;
    case 2: return USBH_SPEED_LOW;
    default: return USBH_SPEED_FULL;
  }
}

USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *phost) { return status(HAL_HCD_ResetPort(phost->pData)); }

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *phost, uint8_t pipe) {
  return HAL_HCD_HC_GetXferCount(phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t epnum, uint8_t dev_address,
                                    uint8_t speed, uint8_t ep_type, uint16_t mps) {
  return status(HAL_HCD_HC_Init(phost->pData, pipe, epnum, dev_address, speed, ep_type, mps));
}

USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe) {
  return status(HAL_HCD_HC_Halt(phost->pData, pipe));
}

USBH_StatusTypeDef USBH_LL_ActivatePipe(USBH_HandleTypeDef *phost, uint8_t pipe) {
  (void)phost;
  (void)pipe;
  return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t direction, uint8_t ep_type,
                                     uint8_t token, uint8_t *pbuff, uint16_t length, uint8_t do_ping) {
  return status(HAL_HCD_HC_SubmitRequest(phost->pData, pipe, direction, ep_type, token, pbuff, length, do_ping));
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *phost, uint8_t pipe) {
  return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState(phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *phost, uint8_t state) {
  (void)phost;
  // Enable des VBUS-Schalters ist aktiv low: state 1 (ein) → PC4 low.
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, state ? GPIO_PIN_RESET : GPIO_PIN_SET);
  HAL_Delay(200);
  return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t toggle) {
  HCD_HandleTypeDef *h = phost->pData;
  if (h->hc[pipe].ep_is_in) h->hc[pipe].toggle_in = toggle;
  else h->hc[pipe].toggle_out = toggle;
  return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe) {
  HCD_HandleTypeDef *h = phost->pData;
  return h->hc[pipe].ep_is_in ? h->hc[pipe].toggle_in : h->hc[pipe].toggle_out;
}

void USBH_Delay(uint32_t ms) {
  HAL_Delay(ms);
}
