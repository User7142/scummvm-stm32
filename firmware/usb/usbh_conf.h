// Konfiguration der ST-USB-Host-Bibliothek (nach usbh_conf_template.h).
// Ohne Betriebssystem, ohne Debugausgaben; Speicher der Klassentreiber
// statisch statt malloc.
#pragma once

#include <stdint.h>
#include <string.h>

#include "stm32f4xx_hal.h"

#define USBH_MAX_NUM_ENDPOINTS 2U
#define USBH_MAX_NUM_INTERFACES 2U
#define USBH_MAX_NUM_CONFIGURATION 1U
#define USBH_KEEP_CFG_DESCRIPTOR 1U
#define USBH_MAX_NUM_SUPPORTED_CLASS 1U
#define USBH_MAX_SIZE_CONFIGURATION 0x200U
#define USBH_MAX_DATA_BUFFER 0x200U
#define USBH_DEBUG_LEVEL 0U
#define USBH_USE_OS 0U
#define USBH_IN_NAK_PROCESS 0

// Die MSC-Klasse holt ihren Zustand einmal per USBH_malloc; ein statischer
// Block genügt (nur eine Klasse aktiv).
void *usbh_static_malloc(uint32_t size);
void usbh_static_free(void *p);
#define USBH_malloc usbh_static_malloc
#define USBH_free usbh_static_free
#define USBH_memset memset
#define USBH_memcpy memcpy

#define USBH_UsrLog(...) do {} while (0)
#define USBH_ErrLog(...) do {} while (0)
#define USBH_DbgLog(...) do {} while (0)
