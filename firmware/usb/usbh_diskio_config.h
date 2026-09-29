// Konfiguration des FatFs-Laufwerkstreibers für USB-Massenspeicher
// (nach drivers/template/usbh_diskio_config.h).
#pragma once

#include "usbh_msc.h"

#define USB_BLOCK_SIZE 512
extern USBH_HandleTypeDef hUsbHost;
#define hUsb_Host hUsbHost
