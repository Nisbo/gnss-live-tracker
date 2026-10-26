#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Ensure GPIO ISR service is installed. Safe to call multiple times.
bool isr_service_ensure_installed(void);

#ifdef __cplusplus
}
#endif
