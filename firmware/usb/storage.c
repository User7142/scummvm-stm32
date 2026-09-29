#include "storage.h"

#include "board.h"
#include "ff_gen_drv.h"
#include "usbh_diskio.h"

USBH_HandleTypeDef hUsbHost;  // Name vom FatFs-USB-Treiber vorgegeben (usbh_diskio_config.h)

static FATFS fs;
static char path[4];
static volatile bool class_active;
static bool mounted;

static void on_event(USBH_HandleTypeDef *host, uint8_t id) {
  (void)host;
  if (id == HOST_USER_CLASS_ACTIVE) class_active = true;
  if (id == HOST_USER_DISCONNECTION) class_active = false;
}

void storage_init(void) {
  if (FATFS_LinkDriver(&USBH_Driver, path) != 0) board_panic(6);
  if (USBH_Init(&hUsbHost, on_event, 0) != USBH_OK) board_panic(6);
  USBH_RegisterClass(&hUsbHost, USBH_MSC_CLASS);
  USBH_Start(&hUsbHost);
}

bool storage_poll(void) {
  USBH_Process(&hUsbHost);
  if (class_active && !mounted) mounted = f_mount(&fs, path, 1) == FR_OK;
  if (!class_active && mounted) {
    f_mount(NULL, path, 0);
    mounted = false;
  }
  return mounted;
}

bool storage_wait(uint32_t timeout_ms) {
  uint32_t start = HAL_GetTick();
  while (!storage_poll()) {
    if (HAL_GetTick() - start > timeout_ms) return false;
  }
  return true;
}

void storage_deinit(void) {
  if (mounted) f_mount(NULL, path, 0);
  mounted = false;
  USBH_Stop(&hUsbHost);
  USBH_DeInit(&hUsbHost);
  FATFS_UnLinkDriver(path);
  HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
}
