/* GNSS Live Tracker V2 modifications, 2026. Based on HTIT Tracker v1.2; see NOTICE and docs/UPSTREAM.md. */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void tft_init(void);
void tft_fill(uint16_t rgb565);
uint16_t tft_width(void);
uint16_t tft_height(void);

// Optional helpers (useful for UI/debug)
void tft_backlight(bool on);
bool tft_set_brightness(uint8_t percent);
bool tft_backlight_is_on(void);
void tft_sleep(void);
void tft_wake(void);
void tft_set_rotation(uint8_t rotation);
void tft_invert_colors(bool invert);
void tft_set_gamma(const uint8_t *gamma_pos, const uint8_t *gamma_neg);

// Simple factory-style display test (no font)
void tft_test_pattern(void);

// Draw a RGB565 bitmap to a region (x1/y1 inclusive). `pixels` is RGB565, big-endian on the wire.
// This is the primitive used by LVGL flush.
void tft_draw_bitmap_rgb565(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, const void *pixels, size_t len_bytes);

// Primitives
void tft_draw_pixel(uint16_t x, uint16_t y, uint16_t rgb565);
void tft_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t rgb565);
void tft_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t rgb565);
void tft_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t rgb565);
void tft_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565);
void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565);
void tft_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t rgb565);
void tft_fill_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t rgb565);
void tft_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t rgb565);
void tft_fill_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t rgb565);
void tft_draw_triangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t rgb565);
void tft_fill_triangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t rgb565);

// Basic 5x7 font
void tft_draw_char(uint16_t x, uint16_t y, char c, uint16_t rgb565, uint16_t bg_rgb565, uint8_t size);
void tft_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t rgb565, uint16_t bg_rgb565, uint8_t size);

#ifdef __cplusplus
}
#endif
