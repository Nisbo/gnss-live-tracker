#pragma once

// Minimal logging shims for LoRaMac sources (avoid Arduino + avoid ST-only licensed headers).

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void lora_printf(const char *format, ...)
{
    (void)format;
    // Intentionally left blank. We keep the symbol to satisfy upstream references.
}

#ifdef __cplusplus
}
#endif

// Upstream macros (kept as no-ops by default)
#define ERR_PRINTF(...)   do {} while (0)
#define WARN_PRINTF(...)  do {} while (0)
#define DBG_PRINTF(...)   do {} while (0)
#define VDBG_PRINTF(...)  do {} while (0)
#define FREQ_PRINTF(...)  do {} while (0)
#define DIO_PRINTF(...)   do {} while (0)
