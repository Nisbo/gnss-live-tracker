/* GNSS Live Tracker V2 modifications, 2026. Based on HTIT Tracker v1.2; see NOTICE and docs/UPSTREAM.md. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t spi_clock_hz;
} sx1262_cfg_t;

typedef struct {bool valid;int8_t requested_dbm,target_dbm,drive_dbm;} sx1262_tx_power_t;
bool sx1262_get_last_tx_power(sx1262_tx_power_t *out);
bool sx1262_init(const sx1262_cfg_t *cfg);
bool sx1262_sleep(void);
bool sx1262_sleep_warm(void);

// --- SX126x opcodes (subset) ---
#define SX126X_CMD_SET_SLEEP                 0x84
#define SX126X_CMD_SET_STANDBY               0x80
#define SX126X_CMD_SET_FS                    0xC1
#define SX126X_CMD_SET_TX                    0x83
#define SX126X_CMD_SET_RX                    0x82
#define SX126X_CMD_SET_PACKET_TYPE           0x8A
#define SX126X_CMD_SET_RF_FREQUENCY          0x86
#define SX126X_CMD_SET_TX_PARAMS             0x8E
#define SX126X_CMD_SET_TX_CONTINUOUS_WAVE    0xD1
#define SX126X_CMD_GET_STATUS                0xC0
#define SX126X_CMD_SET_BUFFER_BASE_ADDRESS   0x8F
#define SX126X_CMD_SET_MODULATION_PARAMS     0x8B
#define SX126X_CMD_SET_PACKET_PARAMS         0x8C
#define SX126X_CMD_SET_DIO_IRQ_PARAMS        0x08
#define SX126X_CMD_GET_IRQ_STATUS            0x12
#define SX126X_CMD_CLR_IRQ_STATUS            0x02
#define SX126X_CMD_WRITE_BUFFER              0x0E
#define SX126X_CMD_READ_BUFFER               0x1E
#define SX126X_CMD_GET_RX_BUFFER_STATUS      0x13
#define SX126X_CMD_GET_PACKET_STATUS         0x14
#define SX126X_CMD_GET_RSSI_INST             0x15
#define SX126X_CMD_WRITE_REGISTER            0x0D
#define SX126X_CMD_READ_REGISTER             0x1D
#define SX126X_CMD_SET_DIO2_AS_RF_SWITCH_CTRL 0x9D
#define SX126X_CMD_SET_REGULATOR_MODE        0x96
#define SX126X_CMD_SET_STOP_RX_TIMER_ON_PREAMBLE 0x9F
#define SX126X_CMD_SET_LORA_SYMB_NUM_TIMEOUT 0xA0

// Registers
#define SX126X_REG_OCP                       0x08E7

// OCP values (see SX126x datasheet)
#define SX126X_OCP_160MA                     0x38

// TX ramp times (subset)
#define SX126X_RAMP_200_US                   0x04
#define SX126X_RAMP_3400_US                  0x07

// Sleep configs (WarmStart keeps radio configuration)
#define SX126X_SLEEP_COLD_START              0x00
#define SX126X_SLEEP_WARM_START              0x04

// IRQ flags
#define SX126X_IRQ_TX_DONE                   0x0001
#define SX126X_IRQ_RX_DONE                   0x0002
#define SX126X_IRQ_PREAMBLE_DETECTED         0x0004
#define SX126X_IRQ_SYNCWORD_VALID            0x0008
#define SX126X_IRQ_HEADER_VALID              0x0010
#define SX126X_IRQ_HEADER_ERROR              0x0020
#define SX126X_IRQ_CRC_ERROR                 0x0040
#define SX126X_IRQ_CAD_DONE                  0x0080
#define SX126X_IRQ_CAD_DETECTED              0x0100
#define SX126X_IRQ_RX_TX_TIMEOUT             0x0200

typedef enum {
    SX126X_PACKET_TYPE_GFSK = 0x00,
    SX126X_PACKET_TYPE_LORA = 0x01,
} sx126x_packet_type_t;

typedef enum {
    SX126X_STANDBY_RC   = 0x00,
    SX126X_STANDBY_XOSC = 0x01,
} sx126x_standby_cfg_t;

// Raw command access (single-CS transaction)
bool sx1262_cmd_write(uint8_t opcode, const uint8_t *buf, size_t len);
bool sx1262_cmd_read(uint8_t opcode, uint8_t *buf, size_t len);

// Register / buffer helpers
bool sx1262_write_register(uint16_t addr, const uint8_t *data, uint8_t len);
bool sx1262_read_register(uint16_t addr, uint8_t *data, uint8_t len);
bool sx1262_write_buffer(uint8_t offset, const uint8_t *data, uint8_t len);
bool sx1262_read_buffer(uint8_t offset, uint8_t *data, uint8_t len);

// High-level helpers used by Radio backend
bool sx1262_set_standby(sx126x_standby_cfg_t cfg);
bool sx1262_set_packet_type(sx126x_packet_type_t type);
bool sx1262_set_rf_frequency(uint32_t freq_hz);
bool sx1262_set_tx_params(int8_t power_dbm, uint8_t ramp_time);
bool sx1262_set_tx_continuous_wave(void);
bool sx1262_set_buffer_base_address(uint8_t tx_base, uint8_t rx_base);
bool sx1262_set_dio_irq_params(uint16_t irq_mask, uint16_t dio1_mask, uint16_t dio2_mask, uint16_t dio3_mask);
bool sx1262_get_irq_status(uint16_t *out);
bool sx1262_clear_irq_status(uint16_t mask);
bool sx1262_set_tx(uint32_t timeout_ms);
bool sx1262_set_rx(uint32_t timeout_ms);
bool sx1262_set_stop_rx_timer_on_preamble(bool enable);
bool sx1262_set_lora_symb_num_timeout(uint8_t symb_num);
bool sx1262_get_rx_buffer_status(uint8_t *payload_len, uint8_t *start_ptr);
bool sx1262_get_packet_status(int16_t *rssi_dbm, int8_t *snr_db);
bool sx1262_get_rssi_inst(int16_t *rssi_dbm);
bool sx1262_set_dio2_as_rf_switch_ctrl(bool enable);
bool sx1262_set_regulator_mode_dcdc(bool enable);
bool sx1262_set_dio3_as_tcxo_ctrl(uint8_t tcxo_voltage, uint32_t delay_ms);
bool sx1262_set_ocp(uint8_t ocp);

// LoRa config helpers (SX126x raw enums)
bool sx1262_set_modulation_params_lora(uint8_t sf, uint8_t bw, uint8_t cr, uint8_t ldro);
bool sx1262_set_packet_params_lora(uint16_t preamble_len, uint8_t header_type, uint8_t payload_len, uint8_t crc_on, uint8_t invert_iq);
bool sx1262_set_pa_config(uint8_t duty_cycle, uint8_t hp_max, uint8_t device_sel, uint8_t pa_lut);

// IRQ plumbing (DIO1 -> task -> callback)
typedef void (*sx1262_dio1_callback_t)(void);
bool sx1262_set_dio1_callback(sx1262_dio1_callback_t cb);

#ifdef __cplusplus
}
#endif
