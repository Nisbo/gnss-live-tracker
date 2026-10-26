#pragma once

// Heltec Wireless Tracker V1.1/V1.2 (ESP32-S3FN8 + SX1262 + UC6580 + ST7735 160x80)

// --- Power / ADC ---
#define PIN_VEXT_CTRL        3
#define PIN_VBAT_READ        1
#define PIN_ADC_CTRL         2

// --- User IO ---
#define PIN_USER_SW          0
#define PIN_LED              18

// --- GNSS UC6580 (UART1 @115200) ---
#define PIN_GNSS_TX          33  // module TX -> MCU RX
#define PIN_GNSS_RX          34  // module RX -> MCU TX
#define PIN_GNSS_RST         35
#define PIN_GNSS_PPS         36
#define PIN_GNSS_D_SEL       48  // UC6580 D_SEL
#define PIN_GNSS_BOOT_MODE   47  // UC6580 BOOT_MODE

// --- LoRa SX1262 (SPI) ---
#define PIN_LORA_NSS         8
#define PIN_LORA_SCK         9
#define PIN_LORA_MOSI        10
#define PIN_LORA_MISO        11
#define PIN_LORA_RST         12
#define PIN_LORA_BUSY        13
#define PIN_LORA_DIO1        14

// --- TFT ST7735 160x80 (SPI) ---
#define PIN_TFT_CS           38
#define PIN_TFT_RES          39
#define PIN_TFT_DC           40
#define PIN_TFT_SCLK         41
#define PIN_TFT_MOSI         42
#define PIN_TFT_BL           21  // LED_K
