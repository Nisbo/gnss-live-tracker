/* GNSS Live Tracker V2 modifications, 2026. Based on HTIT Tracker v1.2; see NOTICE and docs/UPSTREAM.md. */
#include "sx1262_calibration.h"
#include "sx1262.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_rom_sys.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "board_pins.h"
#include "isr_service.h"

static const char *TAG = "sx1262";
static sx1262_tx_power_t configured_power,last_tx_power;
bool sx1262_get_last_tx_power(sx1262_tx_power_t *out){if(!out)return false;*out=last_tx_power;return out->valid;}
#ifdef TRACKER_V2_FEM
// Heltec official Wireless Tracker V2 reference: POWER=7, CSD=4, CTX=5.
static void fem_mode(bool tx, bool enabled) {
    gpio_set_level(GPIO_NUM_7, 1);
    gpio_set_level(GPIO_NUM_4, enabled);
    gpio_set_level(GPIO_NUM_5, tx);
    esp_rom_delay_us(2000);
    if(enabled) ESP_LOGI(TAG,"V2 FEM %s: POWER7=1 CSD4=1 CTX5=%u", tx ? "TX":"RX",tx);
}
#endif

static spi_device_handle_t s_spi = NULL;
static sx1262_dio1_callback_t s_dio1_cb = NULL;
static TaskHandle_t s_dio1_task = NULL;
static bool s_isr_service_installed = false;
static bool s_is_sleeping = false;
static SemaphoreHandle_t s_spi_mutex = NULL;

static esp_err_t sx1262_spi_init(uint32_t clock_hz)
{
    if (s_spi) return ESP_OK;

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_LORA_MOSI,
        .miso_io_num = PIN_LORA_MISO,
        .sclk_io_num = PIN_LORA_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };

    // Use SPI2_HOST (HSPI) by default on ESP32-S3 for custom SPI.
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = (int)clock_hz,
        .mode = 0,
        .spics_io_num = PIN_LORA_NSS,
        .queue_size = 1,
        // Use full-duplex and emulate "cmd then read" with dummy bytes.
    };

    err = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);
    if (err == ESP_OK && s_spi_mutex == NULL) {
        s_spi_mutex = xSemaphoreCreateRecursiveMutex();
        if (!s_spi_mutex) {
            ESP_LOGE(TAG, "SPI mutex alloc failed");
            return ESP_ERR_NO_MEM;
        }
    }
    return err;
}

static void sx1262_gpio_init(void)
{
    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << PIN_LORA_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_conf);
#ifdef TRACKER_V2_FEM
    gpio_config_t fem = {.pin_bit_mask=(1ULL<<7)|(1ULL<<4)|(1ULL<<5), .mode=GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&fem));
    fem_mode(false, false);
#endif

    gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << PIN_LORA_BUSY) | (1ULL << PIN_LORA_DIO1),
        .mode = GPIO_MODE_INPUT,
        // DIO1 should idle low; a weak pulldown helps avoid floating level and missed edge.
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_conf);
}

static void sx1262_reset_pulse(void)
{
    gpio_set_level(PIN_LORA_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LORA_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    s_is_sleeping = false;
}

static bool sx1262_wait_busy_clear(uint32_t timeout_ms)
{
    const uint32_t start = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    while (gpio_get_level(PIN_LORA_BUSY) == 1) {
        vTaskDelay(pdMS_TO_TICKS(1));
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if ((now - start) > timeout_ms) return false;
    }
    return true;
}

static bool sx1262_tx_only(const void *tx, size_t len)
{
    if (!s_spi) return false;
    if (s_spi_mutex) {
        xSemaphoreTakeRecursive(s_spi_mutex, portMAX_DELAY);
    }
    spi_transaction_t t = {
        .length = (int)(len * 8),
        .tx_buffer = tx,
    };
    bool ok = (spi_device_transmit(s_spi, &t) == ESP_OK);
    if (s_spi_mutex) {
        xSemaphoreGiveRecursive(s_spi_mutex);
    }
    return ok;
}

static bool sx1262_txrx_fullduplex(const void *tx, size_t len, void *rx)
{
    if (!s_spi) return false;
    if (s_spi_mutex) {
        xSemaphoreTakeRecursive(s_spi_mutex, portMAX_DELAY);
    }
    spi_transaction_t t = {
        .length = (int)(len * 8),
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    bool ok = (spi_device_transmit(s_spi, &t) == ESP_OK);
    if (s_spi_mutex) {
        xSemaphoreGiveRecursive(s_spi_mutex);
    }
    return ok;
}

static void sx1262_wakeup(void)
{
    if (!s_spi) return;
    uint8_t cmd[2] = { SX126X_CMD_GET_STATUS, 0x00 };
    (void)sx1262_tx_only(cmd, sizeof(cmd));
    (void)sx1262_wait_busy_clear(100);
    s_is_sleeping = false;
}

bool sx1262_cmd_write(uint8_t opcode, const uint8_t *buf, size_t len)
{
    if (!s_spi) return false;
    if (s_is_sleeping) {
        sx1262_wakeup();
    }
    if (!sx1262_wait_busy_clear(100)) return false;

    // Must keep CS asserted for opcode + payload => single SPI transaction.
    uint8_t tmp[1 + 255];
    tmp[0] = opcode;
    if (len > 0 && buf) {
        memcpy(&tmp[1], buf, len);
    }
    bool ok = sx1262_tx_only(tmp, 1 + len);
    if (ok && opcode == SX126X_CMD_SET_SLEEP) {
        s_is_sleeping = true;
    }
    return ok;
}

bool sx1262_cmd_read(uint8_t opcode, uint8_t *buf, size_t len)
{
    if (!s_spi) return false;
    if (s_is_sleeping) {
        sx1262_wakeup();
    }
    if (!sx1262_wait_busy_clear(100)) return false;

    // In ESP-IDF, half-duplex can't be used the way we need here (cmd then read).
    // Use full-duplex and clock out data with dummy bytes:
    // TX: opcode + dummy + (len dummy), RX: discard first 2 bytes then copy len.
    const size_t total = 2 + len;
    if (len > 255) return false;
    uint8_t tx[2 + len];
    uint8_t rx[2 + len];
    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));
    tx[0] = opcode;
    tx[1] = 0x00;
    if (!sx1262_txrx_fullduplex(tx, total, rx)) return false;
    if (buf && len) {
        memcpy(buf, &rx[2], len);
    }
    return true;
}

bool sx1262_sleep(void)
{
    // SetSleep command opcode is 0x84 on SX126x, with 1 byte sleep config.
    const uint8_t sleep_cfg = SX126X_SLEEP_COLD_START;
    bool ok = sx1262_cmd_write(SX126X_CMD_SET_SLEEP, &sleep_cfg, 1);
    if (ok) s_is_sleeping = true;
    return ok;
}

bool sx1262_sleep_warm(void)
{
#ifdef TRACKER_V2_FEM
    fem_mode(false, false);
#endif
    const uint8_t sleep_cfg = SX126X_SLEEP_WARM_START;
    bool ok = sx1262_cmd_write(SX126X_CMD_SET_SLEEP, &sleep_cfg, 1);
    if (ok) s_is_sleeping = true;
    return ok;
}

bool sx1262_set_standby(sx126x_standby_cfg_t cfg)
{
    const uint8_t c = (uint8_t)cfg;
    bool ok = sx1262_cmd_write(SX126X_CMD_SET_STANDBY, &c, 1);
    if (ok) s_is_sleeping = false;
    return ok;
}

bool sx1262_set_packet_type(sx126x_packet_type_t type)
{
    const uint8_t t = (uint8_t)type;
    return sx1262_cmd_write(SX126X_CMD_SET_PACKET_TYPE, &t, 1);
}

static uint8_t s_image_band;
bool sx1262_set_rf_frequency(uint32_t freq_hz)
{
#ifdef TRACKER_V2_FEM
    uint8_t image[2];
    if(!sx1262_image_band(freq_hz,image))return false;
    if(s_image_band!=image[0]){if(!sx1262_cmd_write(0x98,image,2))return false;s_image_band=image[0];}
#endif
    // Freq = freq_hz * 2^25 / 32e6
    uint32_t frf = (uint32_t)((double)freq_hz / 32000000.0 * (double)(1UL << 25));
    uint8_t buf[4] = { (uint8_t)(frf >> 24), (uint8_t)(frf >> 16), (uint8_t)(frf >> 8), (uint8_t)frf };
    return sx1262_cmd_write(SX126X_CMD_SET_RF_FREQUENCY, buf, sizeof(buf));
}

bool sx1262_set_tx_params(int8_t power_dbm, uint8_t ramp_time)
{
    int8_t requested=power_dbm,target=power_dbm;
#ifdef TRACKER_V2_FEM
    // Account for external PA gain rather than applying requested EIRP to SX1262.
    static const uint8_t gain[]={14,14,14,14,14,14,14,14,14,14,14,14,13,13,13,12,12,11,10,9,8,7};
    // Initial EU868 test caps conducted output at 14 dBm.
    if(power_dbm>14) power_dbm=14;
    target=power_dbm;
    for(int i=0;i<22;i++) { if(i+gain[i]>power_dbm || i==21) { power_dbm-=gain[i]; break; } }
    if(power_dbm < -9) power_dbm=-9;
#endif
#ifdef TRACKER_V2_FEM
    ESP_LOGI(TAG, "V2 PA: SX1262 drive=%d dBm (external gain compensated)", power_dbm);
#endif
    uint8_t buf[2] = { (uint8_t)power_dbm, ramp_time };
    bool ok=sx1262_cmd_write(SX126X_CMD_SET_TX_PARAMS, buf, sizeof(buf));
    if(ok)configured_power=(sx1262_tx_power_t){.valid=true,.requested_dbm=requested,.target_dbm=target,.drive_dbm=power_dbm};
    return ok;
}

bool sx1262_set_tx_continuous_wave(void)
{
    return sx1262_cmd_write(SX126X_CMD_SET_TX_CONTINUOUS_WAVE, NULL, 0);
}

bool sx1262_set_pa_config(uint8_t duty_cycle, uint8_t hp_max, uint8_t device_sel, uint8_t pa_lut)
{
    // SetPaConfig opcode is 0x95 on SX126x
    const uint8_t op = 0x95;
    uint8_t buf[4] = { duty_cycle, hp_max, device_sel, pa_lut };
    return sx1262_cmd_write(op, buf, sizeof(buf));
}

bool sx1262_set_modulation_params_lora(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro)
{
    uint8_t buf[4] = { sf, bw, cr, ldro };
    return sx1262_cmd_write(SX126X_CMD_SET_MODULATION_PARAMS, buf, sizeof(buf));
}

bool sx1262_set_packet_params_lora(uint16_t preamble_len, uint8_t header_type, uint8_t payload_len, uint8_t crc_on, uint8_t invert_iq)
{
    uint8_t buf[6] = {
        (uint8_t)(preamble_len >> 8),
        (uint8_t)preamble_len,
        header_type,
        payload_len,
        crc_on,
        invert_iq,
    };
    return sx1262_cmd_write(SX126X_CMD_SET_PACKET_PARAMS, buf, sizeof(buf));
}

bool sx1262_set_buffer_base_address(uint8_t tx_base, uint8_t rx_base)
{
    uint8_t buf[2] = { tx_base, rx_base };
    return sx1262_cmd_write(SX126X_CMD_SET_BUFFER_BASE_ADDRESS, buf, sizeof(buf));
}

bool sx1262_write_buffer(uint8_t offset, const uint8_t *data, uint8_t len)
{
    if (!data && len) return false;
    uint8_t tmp[2 + 255];
    tmp[0] = SX126X_CMD_WRITE_BUFFER;
    tmp[1] = offset;
    if (len) memcpy(&tmp[2], data, len);
    return sx1262_tx_only(tmp, 2 + len);
}

bool sx1262_read_buffer(uint8_t offset, uint8_t *data, uint8_t len)
{
    if (!data && len) return false;
    // TX: opcode + offset + dummy + (len dummy), RX: discard first 3 bytes then copy len.
    const size_t total = 3 + len;
    uint8_t tx[3 + len];
    uint8_t rx[3 + len];
    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));
    tx[0] = SX126X_CMD_READ_BUFFER;
    tx[1] = offset;
    tx[2] = 0x00;
    if (!sx1262_txrx_fullduplex(tx, total, rx)) return false;
    if (len) {
        memcpy(data, &rx[3], len);
    }
    return true;
}

bool sx1262_write_register(uint16_t addr, const uint8_t *data, uint8_t len)
{
    if (!data && len) return false;
    uint8_t tmp[3 + 255];
    tmp[0] = SX126X_CMD_WRITE_REGISTER;
    tmp[1] = (uint8_t)(addr >> 8);
    tmp[2] = (uint8_t)addr;
    if (len) memcpy(&tmp[3], data, len);
    return sx1262_tx_only(tmp, 3 + len);
}

bool sx1262_read_register(uint16_t addr, uint8_t *data, uint8_t len)
{
    if (!data && len) return false;
    // TX: opcode + addr(2) + dummy + (len dummy), RX: discard first 4 bytes then copy len.
    const size_t total = 4 + len;
    uint8_t tx[4 + len];
    uint8_t rx[4 + len];
    memset(tx, 0, sizeof(tx));
    memset(rx, 0, sizeof(rx));
    tx[0] = SX126X_CMD_READ_REGISTER;
    tx[1] = (uint8_t)(addr >> 8);
    tx[2] = (uint8_t)addr;
    tx[3] = 0x00;
    if (!sx1262_txrx_fullduplex(tx, total, rx)) return false;
    if (len) {
        memcpy(data, &rx[4], len);
    }
    return true;
}

bool sx1262_set_dio_irq_params(uint16_t irq_mask, uint16_t dio1_mask, uint16_t dio2_mask, uint16_t dio3_mask)
{
    uint8_t buf[8] = {
        (uint8_t)(irq_mask >> 8), (uint8_t)irq_mask,
        (uint8_t)(dio1_mask >> 8), (uint8_t)dio1_mask,
        (uint8_t)(dio2_mask >> 8), (uint8_t)dio2_mask,
        (uint8_t)(dio3_mask >> 8), (uint8_t)dio3_mask,
    };
    return sx1262_cmd_write(SX126X_CMD_SET_DIO_IRQ_PARAMS, buf, sizeof(buf));
}

bool sx1262_get_irq_status(uint16_t *out)
{
    if (!out) return false;
    uint8_t b[2] = {0};
    if (!sx1262_cmd_read(SX126X_CMD_GET_IRQ_STATUS, b, sizeof(b))) return false;
    *out = (uint16_t)((b[0] << 8) | b[1]);
    return true;
}

bool sx1262_clear_irq_status(uint16_t mask)
{
    uint8_t b[2] = { (uint8_t)(mask >> 8), (uint8_t)mask };
    return sx1262_cmd_write(SX126X_CMD_CLR_IRQ_STATUS, b, sizeof(b));
}

static uint32_t sx1262_timeout_ms_to_rtc(uint32_t timeout_ms)
{
    // SX126x uses 15.625us steps => 1ms = 64 steps
    return timeout_ms * 64U;
}

bool sx1262_set_tx(uint32_t timeout_ms)
{
#ifdef TRACKER_V2_FEM
    fem_mode(true, true);
#endif
    uint32_t t = sx1262_timeout_ms_to_rtc(timeout_ms);
    uint8_t buf[3] = { (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t };
    bool ok=sx1262_cmd_write(SX126X_CMD_SET_TX, buf, sizeof(buf));
    if(ok){last_tx_power=configured_power;}
    return ok;
}

bool sx1262_set_rx(uint32_t timeout_ms)
{
#ifdef TRACKER_V2_FEM
    fem_mode(false, true);
#endif
    uint32_t t = (timeout_ms == 0) ? 0 : sx1262_timeout_ms_to_rtc(timeout_ms);
    uint8_t buf[3] = { (uint8_t)(t >> 16), (uint8_t)(t >> 8), (uint8_t)t };
    return sx1262_cmd_write(SX126X_CMD_SET_RX, buf, sizeof(buf));
}

bool sx1262_set_stop_rx_timer_on_preamble(bool enable)
{
    uint8_t b = enable ? 0x01 : 0x00;
    return sx1262_cmd_write(SX126X_CMD_SET_STOP_RX_TIMER_ON_PREAMBLE, &b, 1);
}

bool sx1262_set_lora_symb_num_timeout(uint8_t symb_num)
{
    return sx1262_cmd_write(SX126X_CMD_SET_LORA_SYMB_NUM_TIMEOUT, &symb_num, 1);
}

bool sx1262_get_rx_buffer_status(uint8_t *payload_len, uint8_t *start_ptr)
{
    uint8_t b[2] = {0};
    if (!sx1262_cmd_read(SX126X_CMD_GET_RX_BUFFER_STATUS, b, sizeof(b))) return false;
    if (payload_len) *payload_len = b[0];
    if (start_ptr) *start_ptr = b[1];
    return true;
}

bool sx1262_get_packet_status(int16_t *rssi_dbm, int8_t *snr_db)
{
    uint8_t b[3] = {0};
    if (!sx1262_cmd_read(SX126X_CMD_GET_PACKET_STATUS, b, sizeof(b))) return false;
    // b[0]=RssiPkt (signed?), b[1]=SnrPkt (signed in 0.25dB), b[2]=SignalRssiPkt
    if (rssi_dbm) *rssi_dbm = (int16_t)(-((int16_t)b[0]) / 2);
    if (snr_db) *snr_db = (int8_t)((int8_t)b[1] / 4);
    return true;
}

bool sx1262_get_rssi_inst(int16_t *rssi_dbm)
{
    if (!rssi_dbm) return false;
    uint8_t b = 0;
    if (!sx1262_cmd_read(SX126X_CMD_GET_RSSI_INST, &b, 1)) return false;
    *rssi_dbm = (int16_t)(-((int16_t)b) / 2);
    return true;
}

bool sx1262_set_dio2_as_rf_switch_ctrl(bool enable)
{
    uint8_t b = enable ? 0x01 : 0x00;
    return sx1262_cmd_write(SX126X_CMD_SET_DIO2_AS_RF_SWITCH_CTRL, &b, 1);
}

bool sx1262_set_regulator_mode_dcdc(bool enable)
{
    uint8_t b = enable ? 0x01 : 0x00; // 1: DC-DC, 0: LDO
    return sx1262_cmd_write(SX126X_CMD_SET_REGULATOR_MODE, &b, 1);
}

bool sx1262_set_dio3_as_tcxo_ctrl(uint8_t tcxo_voltage, uint32_t delay_ms)
{
    // SetDIO3AsTcxoCtrl opcode is 0x97 on SX126x.
    // Params: tcxoVoltage (0..7) + delay (24-bit) in 15.625us steps (1ms = 64 steps)
    const uint8_t op = 0x97;
    uint32_t t = delay_ms * 64U;
    uint8_t buf[4] = {
        tcxo_voltage,
        (uint8_t)(t >> 16),
        (uint8_t)(t >> 8),
        (uint8_t)t,
    };
    return sx1262_cmd_write(op, buf, sizeof(buf));
}

bool sx1262_set_ocp(uint8_t ocp)
{
    return sx1262_write_register(SX126X_REG_OCP, &ocp, 1);
}

static void IRAM_ATTR sx1262_dio1_isr(void *arg)
{
    (void)arg;
    if (!s_dio1_task) return;
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(s_dio1_task, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static void sx1262_dio1_task_fn(void *arg)
{
    (void)arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_dio1_cb) s_dio1_cb();
    }
}

bool sx1262_set_dio1_callback(sx1262_dio1_callback_t cb)
{
    s_dio1_cb = cb;
    return true;
}

bool sx1262_init(const sx1262_cfg_t *cfg)
{
    const uint32_t clock_hz = (cfg && cfg->spi_clock_hz) ? cfg->spi_clock_hz : 8000000;

    sx1262_gpio_init();
    sx1262_reset_pulse();

    esp_err_t err = sx1262_spi_init(clock_hz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(err));
        return false;
    }

    // After reset, BUSY can be high briefly.
    if (!sx1262_wait_busy_clear(500)) {
        ESP_LOGE(TAG, "BUSY stuck after reset");
        return false;
    }

    // Install IRQ service + DIO1 ISR (deferred via task)
    if (s_dio1_task == NULL) {
        // IRQ callback can call into SPI + logging; give it enough stack headroom.
        xTaskCreate(sx1262_dio1_task_fn, "sx1262_dio1", 4096, NULL, 10, &s_dio1_task);
    }
    gpio_set_intr_type(PIN_LORA_DIO1, GPIO_INTR_POSEDGE);
    if (!s_isr_service_installed) {
        s_isr_service_installed = isr_service_ensure_installed();
    }
    esp_err_t add = gpio_isr_handler_add(PIN_LORA_DIO1, sx1262_dio1_isr, NULL);
    if (add != ESP_OK) {
        ESP_LOGW(TAG, "gpio_isr_handler_add(DIO1) failed: %s", esp_err_to_name(add));
    }
    // Be explicit: enable interrupt for this GPIO
    (void)gpio_intr_enable(PIN_LORA_DIO1);

    // Reasonable defaults for Heltec Tracker modules
    (void)sx1262_set_regulator_mode_dcdc(true);
    bool dio2_ok = sx1262_set_dio2_as_rf_switch_ctrl(true);
    ESP_LOGI(TAG, "DIO2 RF switch ctrl: %s", dio2_ok ? "ENABLED" : "FAILED");
    // Many SX1262 modules use a TCXO controlled by DIO3; enable it best-effort.
    // tcxo_voltage: 0x07 => 3.3V, delay ~5ms
#ifdef TRACKER_V2_FEM
    s_image_band=0;
    // Match Heltec's official SX126x initialization (1.8 V TCXO control).
    const uint8_t clear_errors[2]={0,0};
    const uint8_t calibration=0x7F;
        if (!sx1262_set_dio3_as_tcxo_ctrl(0x02, 5) ||
        !sx1262_cmd_write(0x07, clear_errors, 2) ||
        !sx1262_cmd_write(0x89, &calibration, 1) ||
        !sx1262_wait_busy_clear(1000)) {
        ESP_LOGE(TAG, "V2 TCXO calibration failed");
        return false;
    }
    uint8_t errors[2]={0xFF,0xFF};
    if (!sx1262_cmd_read(0x17, errors, 2)) return false;
    ESP_LOGI(TAG,"V2 initialized; device errors=0x%02x%02x",errors[0],errors[1]);
    if (errors[0] || errors[1]) return false;
#else
    (void)sx1262_set_dio3_as_tcxo_ctrl(0x07, 5);
#endif
#ifdef TRACKER_V2_FEM
    // Heltec stays in RC standby here; TX/RX starts the TCXO as required.
    (void)sx1262_set_standby(SX126X_STANDBY_RC);
#else
    (void)sx1262_set_standby(SX126X_STANDBY_XOSC);
#endif

    ESP_LOGI(TAG, "SX1262 init ok (SPI %u Hz)", (unsigned)clock_hz);
    return true;
}
