/* GNSS Live Tracker V2 modifications, 2026. Based on HTIT Tracker v1.2; see NOTICE and docs/UPSTREAM.md. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "LoRaMac.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LORAWAN_SESSION_STORE_NONE = 0,
    LORAWAN_SESSION_STORE_RTC = 1,
    LORAWAN_SESSION_STORE_NVS = 2,
} lorawan_session_store_t;

typedef struct {
    LoRaMacRegion_t region;
    uint8_t as923_group; // 1..4, for AS923.
    uint8_t uplink_dwell; // AU915/AS923: 400ms limit.
    uint8_t channel_block; // US915/AU915: 0 all, 1..8 FSB; ignored for EU868.
    DeviceClass_t device_class;
    lorawan_session_store_t session_store;
    uint16_t session_save_every;
    uint8_t class_b_ping_periodicity;
    bool adr_enabled;
    // Optional overrides (-1/0 = use region defaults)
    int8_t join_datarate;
    uint32_t rx2_frequency;
    int8_t rx2_datarate;
    // Uplink datarate override applied only when ADR is disabled (-1 = use stack default)
    int8_t tx_datarate;
} lorawan_cfg_t;

typedef void (*lorawan_join_cb_t)(bool success);
typedef void (*lorawan_uplink_cb_t)(bool success, uint8_t dr, int16_t rssi);
typedef void (*lorawan_downlink_cb_t)(uint8_t port, const uint8_t *payload, uint8_t len, int16_t rssi, int8_t snr);

typedef struct {
    bool valid;
    bool success;
    uint8_t dr;
    int8_t tx_power;
    uint8_t retries;
    uint32_t counter;
    uint32_t channel;
    uint32_t toa_ms;
} lorawan_uplink_info_t;

typedef struct {
    bool valid;
    uint8_t port;
    uint8_t len;
    uint8_t dr;
    int16_t rssi;
    int8_t snr;
    LoRaMacRxSlot_t slot;
    bool ack;
    bool pending;
    uint32_t counter;
} lorawan_downlink_info_t;

bool lorawan_port_init(const lorawan_cfg_t *cfg);
bool lorawan_port_is_inited(void);
bool lorawan_port_join_otaa(const uint8_t dev_eui[8], const uint8_t app_eui[8], const uint8_t app_key[16], uint8_t nb_trials);
bool lorawan_port_join_abp(uint32_t dev_addr, const uint8_t nwk_skey[16], const uint8_t app_skey[16]);
bool lorawan_port_is_joined(void);
bool lorawan_port_is_join_pending(void);
uint32_t lorawan_port_get_session_clear_seq(void);
void lorawan_port_clear_session(void);

void lorawan_port_set_join_callback(lorawan_join_cb_t cb);
void lorawan_port_set_uplink_callback(lorawan_uplink_cb_t cb);
void lorawan_port_set_downlink_callback(lorawan_downlink_cb_t cb);

bool lorawan_port_send_unconfirmed(uint8_t port, const uint8_t *data, uint8_t len);
bool lorawan_port_send_confirmed(uint8_t port, const uint8_t *data, uint8_t len);
// Send a single uplink, selecting confirmed/unconfirmed explicitly.
bool lorawan_port_send_ex(uint8_t port, const uint8_t *data, uint8_t len, bool confirmed);
// True while an uplink is scheduled/in-flight (duty-cycle backoff or TX running).
bool lorawan_port_is_tx_pending(void);
// True when the stack can accept an uplink of the given length.
bool lorawan_port_can_tx(uint8_t len);

// Uplink confirmation policy (used by lorawan_port_send_auto).
// confirm_every = 0 => always unconfirmed
// confirm_every = 1 => always confirmed
// confirm_every > 1 => confirmed every N uplinks
void lorawan_port_set_uplink_confirm_every(uint16_t confirm_every);
uint16_t lorawan_port_get_uplink_confirm_every(void);
// One-shot override for next uplink (0 = unconfirmed, 1 = confirmed, -1 = clear).
void lorawan_port_set_uplink_confirm_next(int8_t confirmed_next);
int8_t lorawan_port_get_uplink_confirm_next(void);
// Send using the current confirmation policy (and one-shot override if set).
bool lorawan_port_send_auto(uint8_t port, const uint8_t *data, uint8_t len);

void lorawan_port_process(void);

bool lorawan_port_set_class(DeviceClass_t device_class);
DeviceClass_t lorawan_port_get_class(void);
bool lorawan_port_set_adr(bool enable);
bool lorawan_port_get_adr(bool *enabled);
LoRaMacRegion_t lorawan_port_get_region(void);
bool lorawan_port_set_tx_datarate(int8_t dr);
bool lorawan_port_get_tx_datarate(int8_t *dr);
bool lorawan_port_set_tx_sf(uint8_t sf);
bool lorawan_port_sf_to_dr(LoRaMacRegion_t region, uint8_t sf, int8_t *dr);
bool lorawan_port_start_class_b(uint8_t periodicity);
bool lorawan_port_stop_class_b(void);
bool lorawan_port_is_class_b_active(void);
bool lorawan_port_tx_cw(uint32_t freq_hz, int8_t power_dbm, uint16_t duration_s);
void lorawan_port_tx_cw_stop(void);
bool lorawan_port_get_last_uplink(lorawan_uplink_info_t *out);
bool lorawan_port_get_last_downlink(lorawan_downlink_info_t *out);
bool lorawan_port_get_dr_params(uint8_t dr, uint8_t *sf, uint16_t *bw_khz, bool *fsk);

#ifdef __cplusplus
}
#endif

bool lorawan_port_reset_adr_power(void);

LoRaMacRegion_t lorawan_port_region(unsigned application_id);
int lorawan_port_region_id(LoRaMacRegion_t region);
int8_t lorawan_port_baseline_power(void);
unsigned lorawan_port_uplink_dwell(void);
