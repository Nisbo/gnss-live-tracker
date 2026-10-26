/* GNSS Live Tracker V2 modifications, 2026. Based on HTIT Tracker v1.2; see NOTICE and docs/UPSTREAM.md. */
#include "st7735.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "driver/gpio.h"
#ifdef TRACKER_V2_DISPLAY
#include "driver/ledc.h"
static bool s_pwm_ready;
#endif
#include "driver/spi_master.h"

#include "board_pins.h"

static const char *TAG = "st7735";

// ST7735 commands
#define ST7735_SWRESET 0x01
#define ST7735_SLPIN   0x10
#define ST7735_SLPOUT  0x11
#define ST7735_NORON   0x13
#define ST7735_INVOFF  0x20
#define ST7735_INVON   0x21
#define ST7735_DISPOFF 0x28
#define ST7735_DISPON  0x29
#define ST7735_CASET   0x2A
#define ST7735_RASET   0x2B
#define ST7735_RAMWR   0x2C
#define ST7735_MADCTL  0x36
#define ST7735_COLMOD  0x3A

#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR  0xB4
#define ST7735_PWCTR1  0xC0
#define ST7735_PWCTR2  0xC1
#define ST7735_PWCTR3  0xC2
#define ST7735_PWCTR4  0xC3
#define ST7735_PWCTR5  0xC4
#define ST7735_VMCTR1  0xC5
#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1

#define ST7735_MADCTL_MY  0x80
#define ST7735_MADCTL_MX  0x40
#define ST7735_MADCTL_MV  0x20
#define ST7735_MADCTL_BGR 0x08

// 160x80, rotate left. V2 uses a different visible RAM window than V1.x.
static const uint16_t TFT_NATIVE_W = 160;
static const uint16_t TFT_NATIVE_H = 80;
#ifdef TRACKER_V2_DISPLAY
#define TFT_XSTART_DEFAULT 0
#define TFT_YSTART_DEFAULT 24
#else
#define TFT_XSTART_DEFAULT 1
#define TFT_YSTART_DEFAULT 26
#endif
static const uint8_t  TFT_ROT_DEFAULT = (ST7735_MADCTL_MY | ST7735_MADCTL_MV | ST7735_MADCTL_BGR);

static uint16_t s_width = TFT_NATIVE_W;
static uint16_t s_height = TFT_NATIVE_H;
static uint8_t s_xstart = TFT_XSTART_DEFAULT;
static uint8_t s_ystart = TFT_YSTART_DEFAULT;
static uint8_t s_rotation = 1; // 0..3
static bool s_backlight_state = false;

static const uint8_t s_gamma_pos_default[16] = {
    0x02,0x1c,0x07,0x12,0x37,0x32,0x29,0x2d,0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10
};
static const uint8_t s_gamma_neg_default[16] = {
    0x03,0x1d,0x07,0x06,0x2E,0x2C,0x29,0x2D,0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10
};

// 7x10 font data from Heltec reference (ASCII 32..127)
static const uint16_t s_font7x10[] = {
#include "font7x10.inc"
};
static const uint8_t s_font7x10_w = 7;
static const uint8_t s_font7x10_h = 10;

static spi_device_handle_t s_spi = NULL;

static void st_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_TFT_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
}

static void st_data(const void *data, size_t len)
{
    if (len == 0) return;
    gpio_set_level(PIN_TFT_DC, 1);
    spi_transaction_t t = {
        .length = (int)(len * 8),
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
}

static void st_reset(void)
{
    gpio_set_level(PIN_TFT_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(PIN_TFT_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void st_set_addr_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    st_cmd(ST7735_CASET);
    uint8_t data_c[] = { 0x00, (uint8_t)(x0 + s_xstart), 0x00, (uint8_t)(x1 + s_xstart) };
    st_data(data_c, sizeof(data_c));

    st_cmd(ST7735_RASET);
    uint8_t data_r[] = { 0x00, (uint8_t)(y0 + s_ystart), 0x00, (uint8_t)(y1 + s_ystart) };
    st_data(data_r, sizeof(data_r));

    st_cmd(ST7735_RAMWR);
}

static void st_panel_init_sequence(void)
{
    // Init sequence adapted from Heltec HT_st7735.cpp (7735S 160x80)
    st_cmd(ST7735_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    st_cmd(ST7735_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(500));

    uint8_t frm1[] = { 0x01, 0x2C, 0x2D };
    st_cmd(ST7735_FRMCTR1); st_data(frm1, sizeof(frm1));
    st_cmd(ST7735_FRMCTR2); st_data(frm1, sizeof(frm1));

    uint8_t frm3[] = { 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D };
    st_cmd(ST7735_FRMCTR3); st_data(frm3, sizeof(frm3));

    uint8_t invctr[] = { 0x07 };
    st_cmd(ST7735_INVCTR); st_data(invctr, sizeof(invctr));

    uint8_t pw1[] = { 0xA2, 0x02, 0x84 };
    st_cmd(ST7735_PWCTR1); st_data(pw1, sizeof(pw1));
    uint8_t pw2[] = { 0xC5 };
    st_cmd(ST7735_PWCTR2); st_data(pw2, sizeof(pw2));
    uint8_t pw3[] = { 0x0A, 0x00 };
    st_cmd(ST7735_PWCTR3); st_data(pw3, sizeof(pw3));
    uint8_t pw4[] = { 0x8A, 0x2A };
    st_cmd(ST7735_PWCTR4); st_data(pw4, sizeof(pw4));
    uint8_t pw5[] = { 0x8A, 0xEE };
    st_cmd(ST7735_PWCTR5); st_data(pw5, sizeof(pw5));
    uint8_t vm[] = { 0x0E };
    st_cmd(ST7735_VMCTR1); st_data(vm, sizeof(vm));

    st_cmd(ST7735_INVOFF);
    tft_set_rotation(1);

    uint8_t colmod[] = { 0x05 }; // 16-bit
    st_cmd(ST7735_COLMOD); st_data(colmod, sizeof(colmod));

    // Address ranges + invert ON (as Heltec init_cmds2 for 160x80)
    st_cmd(ST7735_CASET);
    uint8_t cas[] = { 0x00, 0x00, 0x00, 0x4F };
    st_data(cas, sizeof(cas));
    st_cmd(ST7735_RASET);
    uint8_t ras[] = { 0x00, 0x00, 0x00, 0x9F };
    st_data(ras, sizeof(ras));
#ifdef TRACKER_V2_DISPLAY
    tft_invert_colors(false);
#else
    tft_invert_colors(true);
#endif

    // Gamma
    tft_set_gamma(s_gamma_pos_default, s_gamma_neg_default);

    st_cmd(ST7735_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));
    st_cmd(ST7735_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void tft_draw_bitmap_rgb565(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, const void *pixels, size_t len_bytes)
{
    if (!pixels || len_bytes == 0) return;
    if (!s_spi) tft_init();
    if (!s_spi) return;

    if (x0 >= s_width || y0 >= s_height) return;
    if (x1 >= s_width) x1 = s_width - 1;
    if (y1 >= s_height) y1 = s_height - 1;
    if (x1 < x0 || y1 < y0) return;

    st_set_addr_window((uint8_t)x0, (uint8_t)y0, (uint8_t)x1, (uint8_t)y1);
    gpio_set_level(PIN_TFT_DC, 1);

    // Send raw RGB565 bytes (big-endian on the wire). Caller must provide correct byte order.
    const uint8_t *p = (const uint8_t *)pixels;
    size_t remaining = len_bytes;
    while (remaining > 0) {
        size_t chunk = remaining;
        if (chunk > 4092) chunk = 4092; // keep under typical DMA limits
        spi_transaction_t t = {
            .length = (int)(chunk * 8),
            .tx_buffer = p,
        };
        ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
        p += chunk;
        remaining -= chunk;
    }
}

bool tft_set_brightness(uint8_t percent)
{
    if(percent>100)return false;
#ifdef TRACKER_V2_DISPLAY
    if(!s_pwm_ready){
        ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_10_BIT,.timer_num=LEDC_TIMER_0,.freq_hz=5000,.clk_cfg=LEDC_AUTO_CLK};
        if(ledc_timer_config(&timer)!=ESP_OK)return false;
        ledc_channel_config_t channel={.gpio_num=PIN_TFT_BL,.speed_mode=LEDC_LOW_SPEED_MODE,.channel=LEDC_CHANNEL_0,.timer_sel=LEDC_TIMER_0,.duty=0,.hpoint=0};
        if(ledc_channel_config(&channel)!=ESP_OK)return false;
        s_pwm_ready=true;
    }
    if(ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,(percent*1023u+50u)/100u)!=ESP_OK||ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0)!=ESP_OK)return false;
    s_backlight_state=percent>0;return true;
#else
    if(percent!=0&&percent!=100)return false;
    tft_backlight(percent>0);return true;
#endif
}

void tft_backlight(bool on)
{
    s_backlight_state = on;
#ifdef TRACKER_V2_DISPLAY
    if(s_pwm_ready){ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,on?1023:0);ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);return;}
#endif
    gpio_set_level(PIN_TFT_BL, on ? 1 : 0);
}

bool tft_backlight_is_on(void)
{
    return s_backlight_state;
}

void tft_sleep(void)
{
    if (!s_spi) {
        return;
    }
    st_cmd(ST7735_DISPOFF);
    vTaskDelay(pdMS_TO_TICKS(10));
    st_cmd(ST7735_SLPIN);
    vTaskDelay(pdMS_TO_TICKS(120));
}

void tft_wake(void)
{
    if (!s_spi) {
        tft_init();
        return;
    }
    // On this panel, a light-sleep wake can leave the controller in a half-awake
    // state ("snow"/random lines). Re-run the full panel init sequence to recover.
    st_reset();
    st_panel_init_sequence();
}

uint16_t tft_width(void)
{
    return s_width;
}

uint16_t tft_height(void)
{
    return s_height;
}

void tft_invert_colors(bool invert)
{
    if (!s_spi) tft_init();
    if (!s_spi) return;
    st_cmd(invert ? ST7735_INVON : ST7735_INVOFF);
}

void tft_set_gamma(const uint8_t *gamma_pos, const uint8_t *gamma_neg)
{
    if (!s_spi) tft_init();
    if (!s_spi) return;
    if (!gamma_pos || !gamma_neg) return;
    st_cmd(ST7735_GMCTRP1);
    st_data(gamma_pos, 16);
    st_cmd(ST7735_GMCTRN1);
    st_data(gamma_neg, 16);
}

void tft_set_rotation(uint8_t rotation)
{
    if (!s_spi) tft_init();
    if (!s_spi) return;

    uint8_t mad = TFT_ROT_DEFAULT;
    switch (rotation & 3) {
        case 0:
            s_width = 80;
            s_height = 160;
            s_xstart = TFT_YSTART_DEFAULT;
            s_ystart = TFT_XSTART_DEFAULT;
            mad = (ST7735_MADCTL_MX | ST7735_MADCTL_MY | ST7735_MADCTL_BGR);
            break;
        case 1:
            s_width = 160;
            s_height = 80;
            s_xstart = TFT_XSTART_DEFAULT;
            s_ystart = TFT_YSTART_DEFAULT;
            mad = (ST7735_MADCTL_MY | ST7735_MADCTL_MV | ST7735_MADCTL_BGR);
            break;
        case 2:
            s_width = 80;
            s_height = 160;
            s_xstart = TFT_YSTART_DEFAULT;
            s_ystart = TFT_XSTART_DEFAULT;
            mad = (ST7735_MADCTL_BGR);
            break;
        case 3:
        default:
            s_width = 160;
            s_height = 80;
            s_xstart = TFT_XSTART_DEFAULT;
            s_ystart = TFT_YSTART_DEFAULT;
            mad = (ST7735_MADCTL_MX | ST7735_MADCTL_MV | ST7735_MADCTL_BGR);
            break;
    }
    s_rotation = (rotation & 3);
    uint8_t data[] = { mad };
    st_cmd(ST7735_MADCTL);
    st_data(data, sizeof(data));
}

void tft_init(void)
{
    if (s_spi) return;

    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << PIN_TFT_DC) | (1ULL << PIN_TFT_RES) | (1ULL << PIN_TFT_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_conf));

    // V2 applies its saved backlight policy after panel initialization.
#ifdef TRACKER_V2_DISPLAY
    tft_backlight(false);
#else
    tft_backlight(true);
#endif

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_TFT_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_TFT_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)(TFT_NATIVE_W * TFT_NATIVE_H * 2),
    };

    // Use a dedicated SPI host for TFT to avoid contention with LoRa.
    esp_err_t err = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20000000,
        .mode = 0,
        .spics_io_num = PIN_TFT_CS,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &devcfg, &s_spi));

    st_reset();
    st_panel_init_sequence();

    ESP_LOGI(TAG, "TFT init ok (%ux%u, offsets %u/%u)", (unsigned)s_width, (unsigned)s_height, s_xstart, s_ystart);
}

void tft_fill(uint16_t rgb565)
{
    if (!s_spi) tft_init();
    if (!s_spi) return;

    st_set_addr_window(0, 0, (uint8_t)(s_width - 1), (uint8_t)(s_height - 1));

    uint8_t hi = (uint8_t)(rgb565 >> 8);
    uint8_t lo = (uint8_t)(rgb565 & 0xFF);

    // Chunked fill to keep stack usage small
    uint8_t buf[512]; // 256 pixels
    for (size_t i = 0; i < sizeof(buf); i += 2) {
        buf[i] = hi;
        buf[i + 1] = lo;
    }

    gpio_set_level(PIN_TFT_DC, 1);
    size_t total_px = (size_t)s_width * (size_t)s_height;
    while (total_px > 0) {
        size_t px = total_px > 256 ? 256 : total_px;
        spi_transaction_t t = {
            .length = (int)(px * 2 * 8),
            .tx_buffer = buf,
        };
        ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
        total_px -= px;
    }
}

void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565)
{
    if (!s_spi) tft_init();
    if (!s_spi) return;
    if (x >= s_width || y >= s_height) return;
    if (x + w > s_width) w = s_width - x;
    if (y + h > s_height) h = s_height - y;
    if (w == 0 || h == 0) return;

    st_set_addr_window((uint8_t)x, (uint8_t)y, (uint8_t)(x + w - 1), (uint8_t)(y + h - 1));

    uint8_t hi = (uint8_t)(rgb565 >> 8);
    uint8_t lo = (uint8_t)(rgb565 & 0xFF);
    uint8_t buf[512]; // 256 pixels
    for (size_t i = 0; i < sizeof(buf); i += 2) {
        buf[i] = hi;
        buf[i + 1] = lo;
    }

    gpio_set_level(PIN_TFT_DC, 1);
    size_t total_px = (size_t)w * (size_t)h;
    while (total_px > 0) {
        size_t px = total_px > 256 ? 256 : total_px;
        spi_transaction_t t = {
            .length = (int)(px * 2 * 8),
            .tx_buffer = buf,
        };
        ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));
        total_px -= px;
    }
}

void tft_test_pattern(void)
{
    // Classic 5-bar test + blink backlight
    tft_init();
    if (!s_spi) return;

    // Colors RGB565
    const uint16_t BLACK = 0x0000;
    const uint16_t WHITE = 0xFFFF;
    const uint16_t RED   = 0xF800;
    const uint16_t GREEN = 0x07E0;
    const uint16_t BLUE  = 0x001F;
    const uint16_t YELL  = 0xFFE0;
    const uint16_t CYAN  = 0x07FF;
    const uint16_t MAG   = 0xF81F;

    const uint16_t colors[5] = { RED, GREEN, BLUE, YELL, MAG };
    const uint16_t bar_w = (uint16_t)(s_width / 5);

    tft_fill(BLACK);
    for (uint16_t i = 0; i < 5; i++) {
        uint16_t x = (uint16_t)(i * bar_w);
        uint16_t w = (i == 4) ? (s_width - x) : bar_w;
        tft_fill_rect(x, 0, w, s_height, colors[i]);
    }

    // White border
    tft_fill_rect(0, 0, s_width, 2, WHITE);
    tft_fill_rect(0, s_height - 2, s_width, 2, WHITE);
    tft_fill_rect(0, 0, 2, s_height, WHITE);
    tft_fill_rect(s_width - 2, 0, 2, s_height, WHITE);

    // Blink backlight 2x
    for (int i = 0; i < 2; i++) {
        tft_backlight(false);
        vTaskDelay(pdMS_TO_TICKS(120));
        tft_backlight(true);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    // Final screen to prove fill works (cyan)
    tft_fill(CYAN);
}

void tft_draw_pixel(uint16_t x, uint16_t y, uint16_t rgb565)
{
    if (!s_spi) tft_init();
    if (!s_spi) return;
    if (x >= s_width || y >= s_height) return;

    st_set_addr_window((uint8_t)x, (uint8_t)y, (uint8_t)x, (uint8_t)y);
    uint8_t data[] = { (uint8_t)(rgb565 >> 8), (uint8_t)(rgb565 & 0xFF) };
    st_data(data, sizeof(data));
}

void tft_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t rgb565)
{
    tft_fill_rect(x, y, w, 1, rgb565);
}

void tft_draw_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t rgb565)
{
    tft_fill_rect(x, y, 1, h, rgb565);
}

void tft_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t rgb565)
{
    int16_t dx = (x0 < x1) ? (int16_t)(x1 - x0) : (int16_t)(x0 - x1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t dy = (y0 < y1) ? (int16_t)(y1 - y0) : (int16_t)(y0 - y1);
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = (dx > dy ? dx : -dy) / 2;

    while (1) {
        tft_draw_pixel(x0, y0, rgb565);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = err;
        if (e2 > -dx) { err -= dy; x0 = (uint16_t)(x0 + sx); }
        if (e2 < dy)  { err += dx; y0 = (uint16_t)(y0 + sy); }
    }
}

void tft_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t rgb565)
{
    if (w == 0 || h == 0) return;
    tft_draw_hline(x, y, w, rgb565);
    tft_draw_hline(x, (uint16_t)(y + h - 1), w, rgb565);
    tft_draw_vline(x, y, h, rgb565);
    tft_draw_vline((uint16_t)(x + w - 1), y, h, rgb565);
}

void tft_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t rgb565)
{
    int16_t f = 1 - (int16_t)r;
    int16_t ddF_x = 1;
    int16_t ddF_y = (int16_t)(-2 * r);
    int16_t x = 0;
    int16_t y = (int16_t)r;

    tft_draw_pixel(x0, (uint16_t)(y0 + r), rgb565);
    tft_draw_pixel(x0, (uint16_t)(y0 - r), rgb565);
    tft_draw_pixel((uint16_t)(x0 + r), y0, rgb565);
    tft_draw_pixel((uint16_t)(x0 - r), y0, rgb565);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        tft_draw_pixel((uint16_t)(x0 + x), (uint16_t)(y0 + y), rgb565);
        tft_draw_pixel((uint16_t)(x0 - x), (uint16_t)(y0 + y), rgb565);
        tft_draw_pixel((uint16_t)(x0 + x), (uint16_t)(y0 - y), rgb565);
        tft_draw_pixel((uint16_t)(x0 - x), (uint16_t)(y0 - y), rgb565);
        tft_draw_pixel((uint16_t)(x0 + y), (uint16_t)(y0 + x), rgb565);
        tft_draw_pixel((uint16_t)(x0 - y), (uint16_t)(y0 + x), rgb565);
        tft_draw_pixel((uint16_t)(x0 + y), (uint16_t)(y0 - x), rgb565);
        tft_draw_pixel((uint16_t)(x0 - y), (uint16_t)(y0 - x), rgb565);
    }
}

void tft_fill_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t rgb565)
{
    tft_draw_vline(x0, (uint16_t)(y0 - r), (uint16_t)(2 * r + 1), rgb565);

    int16_t f = 1 - (int16_t)r;
    int16_t ddF_x = 1;
    int16_t ddF_y = (int16_t)(-2 * r);
    int16_t x = 0;
    int16_t y = (int16_t)r;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        tft_draw_vline((uint16_t)(x0 + x), (uint16_t)(y0 - y), (uint16_t)(2 * y + 1), rgb565);
        tft_draw_vline((uint16_t)(x0 - x), (uint16_t)(y0 - y), (uint16_t)(2 * y + 1), rgb565);
        tft_draw_vline((uint16_t)(x0 + y), (uint16_t)(y0 - x), (uint16_t)(2 * x + 1), rgb565);
        tft_draw_vline((uint16_t)(x0 - y), (uint16_t)(y0 - x), (uint16_t)(2 * x + 1), rgb565);
    }
}

void tft_draw_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t rgb565)
{
    if (r > (w / 2)) r = w / 2;
    if (r > (h / 2)) r = h / 2;
    tft_draw_rect((uint16_t)(x + r), y, (uint16_t)(w - 2 * r), h, rgb565);
    tft_draw_circle((uint16_t)(x + r), (uint16_t)(y + r), r, rgb565);
    tft_draw_circle((uint16_t)(x + w - r - 1), (uint16_t)(y + r), r, rgb565);
    tft_draw_circle((uint16_t)(x + r), (uint16_t)(y + h - r - 1), r, rgb565);
    tft_draw_circle((uint16_t)(x + w - r - 1), (uint16_t)(y + h - r - 1), r, rgb565);
}

void tft_fill_round_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint16_t rgb565)
{
    if (r > (w / 2)) r = w / 2;
    if (r > (h / 2)) r = h / 2;
    tft_fill_rect((uint16_t)(x + r), y, (uint16_t)(w - 2 * r), h, rgb565);
    tft_fill_circle((uint16_t)(x + r), (uint16_t)(y + r), r, rgb565);
    tft_fill_circle((uint16_t)(x + w - r - 1), (uint16_t)(y + r), r, rgb565);
    tft_fill_circle((uint16_t)(x + r), (uint16_t)(y + h - r - 1), r, rgb565);
    tft_fill_circle((uint16_t)(x + w - r - 1), (uint16_t)(y + h - r - 1), r, rgb565);
}

static void swap_u16(uint16_t *a, uint16_t *b)
{
    uint16_t t = *a;
    *a = *b;
    *b = t;
}

void tft_draw_triangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t rgb565)
{
    tft_draw_line(x0, y0, x1, y1, rgb565);
    tft_draw_line(x1, y1, x2, y2, rgb565);
    tft_draw_line(x2, y2, x0, y0, rgb565);
}

void tft_fill_triangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t rgb565)
{
    if (y0 > y1) { swap_u16(&y0, &y1); swap_u16(&x0, &x1); }
    if (y1 > y2) { swap_u16(&y2, &y1); swap_u16(&x2, &x1); }
    if (y0 > y1) { swap_u16(&y0, &y1); swap_u16(&x0, &x1); }

    if (y0 == y2) {
        uint16_t minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        uint16_t maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        tft_draw_hline(minx, y0, (uint16_t)(maxx - minx + 1), rgb565);
        return;
    }

    int32_t dx01 = (int32_t)x1 - x0;
    int32_t dy01 = (int32_t)y1 - y0;
    int32_t dx02 = (int32_t)x2 - x0;
    int32_t dy02 = (int32_t)y2 - y0;
    int32_t dx12 = (int32_t)x2 - x1;
    int32_t dy12 = (int32_t)y2 - y1;

    int32_t sa = 0;
    int32_t sb = 0;

    uint16_t y;
    uint16_t last = (y1 == y2) ? y1 : (uint16_t)(y1 - 1);
    for (y = y0; y <= last; y++) {
        uint16_t a = (uint16_t)(x0 + sa / dy01);
        uint16_t b = (uint16_t)(x0 + sb / dy02);
        sa += dx01;
        sb += dx02;
        if (a > b) swap_u16(&a, &b);
        tft_draw_hline(a, y, (uint16_t)(b - a + 1), rgb565);
    }

    sa = dx12 * (int32_t)(y - y1);
    sb = dx02 * (int32_t)(y - y0);
    for (; y <= y2; y++) {
        uint16_t a = (uint16_t)(x1 + sa / dy12);
        uint16_t b = (uint16_t)(x0 + sb / dy02);
        sa += dx12;
        sb += dx02;
        if (a > b) swap_u16(&a, &b);
        tft_draw_hline(a, y, (uint16_t)(b - a + 1), rgb565);
    }
}

void tft_draw_char(uint16_t x, uint16_t y, char c, uint16_t rgb565, uint16_t bg_rgb565, uint8_t size)
{
    if (!s_spi) tft_init();
    if (!s_spi) return;
    if (c < 32 || c > 127) c = '?';
    if (size == 0) size = 1;

    uint32_t idx = (uint32_t)(c - 32) * s_font7x10_h;
    for (uint8_t row = 0; row < s_font7x10_h; row++) {
        uint16_t bits = s_font7x10[idx + row];
        for (uint8_t col = 0; col < s_font7x10_w; col++) {
            bool on = ((bits << col) & 0x8000u) != 0;
            uint16_t color = on ? rgb565 : bg_rgb565;
            if (size == 1) {
                tft_draw_pixel((uint16_t)(x + col), (uint16_t)(y + row), color);
            } else {
                tft_fill_rect((uint16_t)(x + col * size), (uint16_t)(y + row * size), size, size, color);
            }
        }
    }
}

void tft_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t rgb565, uint16_t bg_rgb565, uint8_t size)
{
    if (!text) return;
    uint16_t cx = x;
    uint16_t cy = y;
    uint16_t step_x = (uint16_t)(s_font7x10_w * (size ? size : 1));
    uint16_t step_y = (uint16_t)(s_font7x10_h * (size ? size : 1));

    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            cx = x;
            cy = (uint16_t)(cy + step_y);
            continue;
        }
        tft_draw_char(cx, cy, *p, rgb565, bg_rgb565, size);
        cx = (uint16_t)(cx + step_x);
        if (cx + step_x >= s_width) {
            cx = x;
            cy = (uint16_t)(cy + step_y);
        }
    }
}
