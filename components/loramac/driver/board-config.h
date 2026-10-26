#pragma once

// Minimal board config for our ESP-IDF native port (no Heltec proprietary code).
// This header exists because the upstream LoRaMac sources include ../driver/board-config.h.

// Target radio: SX1262 on Heltec Wireless Tracker V1.1/V1.2
#ifndef RADIO_CHIP_SX1262
#define RADIO_CHIP_SX1262 1
#endif
