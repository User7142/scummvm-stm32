// ScummVM-Backend für das STM32F429I-Discovery.
#pragma once

#include "common/system.h"

// Erwartet: Display, Touch, USER-Taste und USB-Stick sind initialisiert.
OSystem *stm32_system_create();
