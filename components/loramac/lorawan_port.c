/* GNSS Live Tracker V2 modifications, 2026. Based on HTIT Tracker v1.2; see NOTICE and docs/UPSTREAM.md. */
#include "region_policy.h"
#include "region/RegionAS923.h"
#include "lorawan_port.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "radio.h"
#include "LoRaMacClassB.h"
#include "RegionEU868.h"

#include <stddef.h>
#include <string.h>

static const char *TAG = "lorawan";

static lorawan_join_cb_t s_join_cb = NULL;
static lorawan_uplink_cb_t s_uplink_cb = NULL;
static lorawan_downlink_cb_t s_downlink_cb = NULL;
static bool s_inited = false;
static LoRaMacRegion_t s_region = LORAMAC_REGION_EU868;
static DeviceClass_t s_class = CLASS_A;
static DeviceClass_t s_target_class = CLASS_A;
static lorawan_session_store_t s_session_store = LORAWAN_SESSION_STORE_NONE;
static uint16_t s_session_save_every = 0;
static uint16_t s_session_save_counter = 0;
static uint8_t s_class_b_ping_periodicity = 0;
static LoRaMacPrimitives_t s_mac_primitives = {0};
static LoRaMacCallback_t s_mac_callbacks = {0};
static lorawan_uplink_info_t s_last_uplink = {0};
static lorawan_downlink_info_t s_last_downlink = {0};
static uint16_t s_uplink_confirm_every = 0;
static uint32_t s_uplink_counter = 0;
static int8_t s_uplink_confirm_next = -1;
static uint32_t s_last_tx_busy_log_ms = 0;
static bool s_tx_pending = false;
static uint32_t s_tx_pending_since_ms = 0;
static int8_t s_tx_datarate = -1;
static uint8_t s_channel_block,s_as_group=1,s_initial_dwell;
static volatile bool s_join_pending = false;
static volatile uint32_t s_session_clear_seq = 0;

static void lorawan_reset_duty_cycle_backoff(const char *reason)
{
    LoRaMacResetBackoff();
    if (s_region == LORAMAC_REGION_EU868) {
        RegionEU868ResetBands();
    }
    ESP_LOGW(TAG, "Duty-cycle backoff reset (%s)", reason ? reason : "unknown");
}

typedef enum {
    CLASS_B_IDLE = 0,
    CLASS_B_WAIT_DEVICE_TIME,
    CLASS_B_WAIT_BEACON_ACQ,
    CLASS_B_WAIT_PING_SLOT,
    CLASS_B_ACTIVE,
} lorawan_class_b_state_t;

static lorawan_class_b_state_t s_class_b_state = CLASS_B_IDLE;

static bool lorawan_force_default_channels(void)
{
#if defined(LORAWAN_EU868_FORCE_DEFAULT_CHANNELS)
    return (LORAWAN_EU868_FORCE_DEFAULT_CHANNELS != 0) && (s_region == LORAMAC_REGION_EU868);
#else
    return false;
#endif
}

static void lorawan_apply_default_channels(const char *reason)
{
    if (!lorawan_force_default_channels()) return;
    uint16_t mask_val = 0x0007; // EU868 default: 868.1/868.3/868.5
#if defined(LORAWAN_EU868_CHANNELS_MASK)
    mask_val = (uint16_t)LORAWAN_EU868_CHANNELS_MASK;
#endif
    if (mask_val == 0) {
        ESP_LOGW(TAG, "EU868 mask is 0x0000, skipping (%s)", reason ? reason : "unknown");
        return;
    }
    uint16_t mask[1] = { mask_val };
    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_CHANNELS_MASK;
    mib.Param.ChannelsMask = mask;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        ESP_LOGW(TAG, "EU868 mask apply failed (%s)", reason ? reason : "unknown");
        return;
    }
    mib.Type = MIB_CHANNELS_DEFAULT_MASK;
    mib.Param.ChannelsDefaultMask = mask;
    (void)LoRaMacMibSetRequestConfirm(&mib);
    ESP_LOGW(TAG, "EU868 channels forced to mask (0x%04X) [%s]", mask[0],
             reason ? reason : "unknown");
}

static bool lorawan_us_channels(void){
 if(s_region!=LORAMAC_REGION_US915&&s_region!=LORAMAC_REGION_AU915)return true;
 uint16_t mask[6];if(!tracker_us915_mask(s_channel_block,mask))return false;
 MibRequestConfirm_t mib={.Type=MIB_CHANNELS_DEFAULT_MASK};mib.Param.ChannelsDefaultMask=mask;
 if(LoRaMacMibSetRequestConfirm(&mib)!=LORAMAC_STATUS_OK)return false;
 mib.Type=MIB_CHANNELS_MASK;mib.Param.ChannelsMask=mask;
 return LoRaMacMibSetRequestConfirm(&mib)==LORAMAC_STATUS_OK;
}
static uint32_t lorawan_network_plan(void){return ((uint32_t)s_region<<8)|s_channel_block|(s_region==LORAMAC_REGION_AS923?(uint32_t)s_as_group<<16:0)|((uint32_t)s_initial_dwell<<24);}

bool lorawan_port_get_dr_params(uint8_t dr, uint8_t *sf, uint16_t *bw_khz, bool *fsk);

static bool lorawan_apply_tx_datarate_if_needed(const char *reason)
{
    if (!s_inited) return false;
    if (s_tx_datarate < 0) return false;
    bool adr = false;
    if (lorawan_port_get_adr(&adr) && adr) {
        return false;
    }
    if (!lorawan_port_is_joined()) {
        return false;
    }

    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_CHANNELS_DATARATE;
    GetPhyParams_t minimum={.Attribute=PHY_MIN_TX_DR,.UplinkDwellTime=LoRaMacUplinkDwell()};
    int8_t effective=MAX(s_tx_datarate,(int8_t)RegionGetPhyParam(s_region,&minimum).Value);
    mib.Param.ChannelsDatarate = effective;
    LoRaMacStatus_t st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        ESP_LOGW(TAG, "TX DR override failed: %d (%s)", (int)st, reason ? reason : "unknown");
        return false;
    }
    uint8_t sf = 0;
    uint16_t bw_khz = 0;
    bool fsk = false;
    if (lorawan_port_get_dr_params((uint8_t)s_tx_datarate, &sf, &bw_khz, &fsk)) {
        if (fsk) {
            ESP_LOGI(TAG, "TX DR override set to %d (FSK BW%u) (%s)",
                     (int)s_tx_datarate, (unsigned)bw_khz, reason ? reason : "unknown");
        } else {
            ESP_LOGI(TAG, "TX DR override set to %d (SF%u BW%u) (%s)",
                     (int)s_tx_datarate, (unsigned)sf, (unsigned)bw_khz,
                     reason ? reason : "unknown");
        }
    } else {
        ESP_LOGI(TAG, "TX DR override set to %d (%s)", (int)s_tx_datarate,
                 reason ? reason : "unknown");
    }
    return true;
}

#define LORAWAN_SESSION_MAGIC      0x4C4F5241U
#define LORAWAN_SESSION_VERSION    1
#define LORAWAN_SESSION_NVS_NS     "lorawan"
#define LORAWAN_SESSION_NVS_KEY    "session"
#define LORAWAN_CHANNELS_MASK_MAX  6

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t region;
    uint8_t device_class;
    uint8_t channels_mask_len;
    uint32_t dev_addr;
    uint32_t net_id;
    uint32_t uplink_counter;
    uint32_t downlink_counter;
    int8_t datarate;
    int8_t tx_power;
    uint8_t adr_enabled;
    uint8_t public_network;
    Rx2ChannelParams_t rx2;
    uint16_t channels_mask[LORAWAN_CHANNELS_MASK_MAX];
    uint8_t nwk_skey[16];
    uint8_t app_skey[16];
    uint32_t crc32;
} lorawan_session_t;

static size_t lorawan_region_mask_len(LoRaMacRegion_t region)
{
    switch (region) {
        case LORAMAC_REGION_US915:
        case LORAMAC_REGION_US915_HYBRID:
        case LORAMAC_REGION_AU915:
        case LORAMAC_REGION_CN470:
            return 6;
        default:
            return 1;
    }
}

/* Additional context keeps dynamic CFList/NewChannelReq channels and negotiated
   dwell/RX1/repetition parameters across a reboot without changing legacy blobs. */
/* Version 2 requires complete, negotiated receive parameters. Version 1 may
   contain default RX delays inherited from a legacy session restore. */
#define LORAWAN_RX_CONTEXT_VERSION 2

typedef struct {uint32_t version,plan;LoRaMacRegionalState_t state;uint8_t count,reserved[3];ChannelParams_t channels[16];uint32_t crc;} regional_context_t;
static void regional_capture(regional_context_t *c){
 memset(c,0,sizeof(*c));c->version=LORAWAN_RX_CONTEXT_VERSION;c->plan=lorawan_network_plan();LoRaMacGetRegionalState(&c->state);
 if(s_region!=LORAMAC_REGION_US915&&s_region!=LORAMAC_REGION_AU915){GetPhyParams_t get={.Attribute=PHY_CHANNELS};c->count=16;memcpy(c->channels,RegionGetPhyParam(s_region,&get).Channels,sizeof(c->channels));}
 c->crc=esp_rom_crc32_le(0,(const uint8_t *)c,offsetof(regional_context_t,crc));
}
static bool regional_restore(void){
 regional_context_t c;nvs_handle_t h;size_t n=sizeof(c);if(nvs_open(LORAWAN_SESSION_NVS_NS,NVS_READONLY,&h)!=ESP_OK)return false;
 esp_err_t e=nvs_get_blob(h,"context",&c,&n);nvs_close(h);
 if(e==ESP_ERR_NVS_NOT_FOUND)return false;
 if(e!=ESP_OK||n!=sizeof(c)||c.version!=LORAWAN_RX_CONTEXT_VERSION||c.plan!=lorawan_network_plan()||c.crc!=esp_rom_crc32_le(0,(const uint8_t *)&c,offsetof(regional_context_t,crc))||c.count!=(tracker_region_fsb(lorawan_port_region_id(s_region))?0:16))return false;
 unsigned id=lorawan_port_region_id(s_region);
 for(unsigned i=0;i<c.count;i++){ChannelParams_t *p=&c.channels[i];if(p->Frequency&&(!tracker_region_frequency(id,p->Frequency)||(p->Rx1Frequency&&!tracker_region_frequency(id,p->Rx1Frequency))||p->DrRange.Fields.Min>p->DrRange.Fields.Max||p->DrRange.Fields.Max>(id==4?5:7)))return false;}
 if(!LoRaMacRestoreRegionalState(&c.state))return false;
 for(unsigned i=0;i<c.count;i++)if(c.channels[i].Frequency&&LoRaMacChannelAdd(i,c.channels[i])!=LORAMAC_STATUS_OK)return false;
 return true;
}

static uint32_t lorawan_session_crc(const lorawan_session_t *session)
{
    if (!session) return 0;
    return esp_rom_crc32_le(0, (const uint8_t *)session,
                            offsetof(lorawan_session_t, crc32));
}

static bool lorawan_session_capture(lorawan_session_t *out)
{
    if (!out || !s_inited) return false;

    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_NETWORK_JOINED;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK ||
        !mib.Param.IsNetworkJoined) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->magic = LORAWAN_SESSION_MAGIC;
    out->version = LORAWAN_SESSION_VERSION;
    out->region = (uint8_t)s_region;

    mib.Type = MIB_DEVICE_CLASS;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        out->device_class = (uint8_t)mib.Param.Class;
    } else {
        out->device_class = (uint8_t)s_class;
    }

    mib.Type = MIB_DEV_ADDR;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;
    out->dev_addr = mib.Param.DevAddr;

    mib.Type = MIB_NET_ID;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        out->net_id = mib.Param.NetID;
    }

    mib.Type = MIB_NWK_SKEY;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK || !mib.Param.NwkSKey) {
        return false;
    }
    memcpy(out->nwk_skey, mib.Param.NwkSKey, sizeof(out->nwk_skey));

    mib.Type = MIB_APP_SKEY;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK || !mib.Param.AppSKey) {
        return false;
    }
    memcpy(out->app_skey, mib.Param.AppSKey, sizeof(out->app_skey));

    mib.Type = MIB_UPLINK_COUNTER;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;
    out->uplink_counter = mib.Param.UpLinkCounter;

    mib.Type = MIB_DOWNLINK_COUNTER;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;
    out->downlink_counter = mib.Param.DownLinkCounter;

    mib.Type = MIB_ADR;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        out->adr_enabled = mib.Param.AdrEnable ? 1 : 0;
    }

    mib.Type = MIB_PUBLIC_NETWORK;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        out->public_network = mib.Param.EnablePublicNetwork ? 1 : 0;
    }

    mib.Type = MIB_CHANNELS_DATARATE;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        out->datarate = mib.Param.ChannelsDatarate;
    }

    mib.Type = MIB_CHANNELS_TX_POWER;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        out->tx_power = mib.Param.ChannelsTxPower;
    }

    mib.Type = MIB_RX2_CHANNEL;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        out->rx2 = mib.Param.Rx2Channel;
    }

    size_t mask_len = lorawan_region_mask_len(s_region);
    out->channels_mask_len = (uint8_t)mask_len;
    mib.Type = MIB_CHANNELS_MASK;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK && mib.Param.ChannelsMask) {
        memcpy(out->channels_mask, mib.Param.ChannelsMask, mask_len * sizeof(uint16_t));
    }

    out->crc32 = lorawan_session_crc(out);
    return true;
}

static bool lorawan_session_apply(const lorawan_session_t *session)
{
    if (!session || !s_inited) return false;
    if (session->magic != LORAWAN_SESSION_MAGIC) return false;
    if (session->version != LORAWAN_SESSION_VERSION) return false;
    if (session->region != (uint8_t)s_region) {
        ESP_LOGW(TAG, "NVS session region mismatch (nvs=%u cfg=%u)",
                 (unsigned)session->region, (unsigned)s_region);
        return false;
    }

    MibRequestConfirm_t mib = {0};

    mib.Type = MIB_DEV_ADDR;
    mib.Param.DevAddr = session->dev_addr;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;

    mib.Type = MIB_NET_ID;
    mib.Param.NetID = session->net_id;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_NWK_SKEY;
    mib.Param.NwkSKey = (uint8_t *)session->nwk_skey;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;

    mib.Type = MIB_APP_SKEY;
    mib.Param.AppSKey = (uint8_t *)session->app_skey;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;

    mib.Type = MIB_UPLINK_COUNTER;
    mib.Param.UpLinkCounter = session->uplink_counter;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;

    mib.Type = MIB_DOWNLINK_COUNTER;
    mib.Param.DownLinkCounter = session->downlink_counter;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;

    mib.Type = MIB_ADR;
    mib.Param.AdrEnable = (session->adr_enabled != 0);
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_PUBLIC_NETWORK;
    mib.Param.EnablePublicNetwork = (session->public_network != 0);
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_CHANNELS_DATARATE;
    mib.Param.ChannelsDatarate = session->datarate;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_CHANNELS_TX_POWER;
    mib.Param.ChannelsTxPower = session->tx_power;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_RX2_CHANNEL;
    mib.Param.Rx2Channel = session->rx2;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_CHANNELS_MASK;
    mib.Param.ChannelsMask = (uint16_t *)session->channels_mask;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_DEVICE_CLASS;
    mib.Param.Class = (DeviceClass_t)session->device_class;
    (void)LoRaMacMibSetRequestConfirm(&mib);
    s_class = (DeviceClass_t)session->device_class;

    mib.Type = MIB_NETWORK_JOINED;
    mib.Param.IsNetworkJoined = true;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;

    lorawan_reset_duty_cycle_backoff("session restore");

    ESP_LOGI(TAG, "NVS session restored (devaddr=0x%08X)", (unsigned)session->dev_addr);
    return true;
}

static void lorawan_session_clear_nvs(void);

static bool lorawan_session_restore_nvs(void)
{
    lorawan_session_t session;
    size_t len = sizeof(session);
    nvs_handle_t handle;
    if (nvs_open(LORAWAN_SESSION_NVS_NS, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_blob(handle, LORAWAN_SESSION_NVS_KEY, &session, &len);
    uint32_t plan=0;esp_err_t plan_err=nvs_get_u32(handle,"plan",&plan);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(session)) return false;
    if(plan_err==ESP_OK?plan!=lorawan_network_plan():!(plan_err==ESP_ERR_NVS_NOT_FOUND&&s_region==LORAMAC_REGION_EU868))return false;

    uint32_t crc = lorawan_session_crc(&session);
    if (crc != session.crc32) {
        ESP_LOGW(TAG, "NVS session CRC mismatch");
        lorawan_session_clear_nvs();
        return false;
    }
    if (!regional_restore()||!lorawan_session_apply(&session)) {
        RegionInitDefaults(s_region,INIT_TYPE_INIT);
        lorawan_session_clear_nvs();
        return false;
    }

    // Check for standalone frame counters that are newer than the blob's.
    {
        nvs_handle_t h2;
        if (nvs_open(LORAWAN_SESSION_NVS_NS, NVS_READONLY, &h2) == ESP_OK) {
            uint32_t fcnt = 0;
            if (nvs_get_u32(h2, "fcnt", &fcnt) == ESP_OK && fcnt > session.uplink_counter) {
                MibRequestConfirm_t mib;
                mib.Type = MIB_UPLINK_COUNTER;
                mib.Param.UpLinkCounter = fcnt;
                if (LoRaMacMibSetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
                    ESP_LOGI(TAG, "NVS fcnt_up override: %u -> %u",
                             (unsigned)session.uplink_counter, (unsigned)fcnt);
                }
            }
            uint32_t fcnt_dn = 0;
            if (nvs_get_u32(h2, "fcnt_dn", &fcnt_dn) == ESP_OK && fcnt_dn > session.downlink_counter) {
                MibRequestConfirm_t mib;
                mib.Type = MIB_DOWNLINK_COUNTER;
                mib.Param.DownLinkCounter = fcnt_dn;
                if (LoRaMacMibSetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
                    ESP_LOGI(TAG, "NVS fcnt_dn override: %u -> %u",
                             (unsigned)session.downlink_counter, (unsigned)fcnt_dn);
                }
            }
            nvs_close(h2);
        }
    }

    return true;
}

static void lorawan_session_clear_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(LORAWAN_SESSION_NVS_NS, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    (void)nvs_erase_key(handle, LORAWAN_SESSION_NVS_KEY);
    (void)nvs_erase_key(handle, "fcnt");
    (void)nvs_erase_key(handle, "fcnt_dn");
    (void)nvs_erase_key(handle, "plan");
    (void)nvs_erase_key(handle,"context");
    (void)nvs_commit(handle);
    nvs_close(handle);
}

static bool lorawan_session_save_nvs(void)
{
    lorawan_session_t session;
    if (!lorawan_session_capture(&session)) return false;

    nvs_handle_t handle;
    if (nvs_open(LORAWAN_SESSION_NVS_NS, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(handle, LORAWAN_SESSION_NVS_KEY, &session, sizeof(session));
    if (err == ESP_OK)err=nvs_set_u32(handle,"plan",lorawan_network_plan());
    if(err==ESP_OK){regional_context_t context;regional_capture(&context);err=nvs_set_blob(handle,"context",&context,sizeof(context));}
    if (err == ESP_OK) {
        // Clear standalone counter keys on full save (they are now redundant).
        (void)nvs_erase_key(handle, "fcnt");
        (void)nvs_erase_key(handle, "fcnt_dn");
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS session save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// Save only the frame counters between full session saves.
// Writes 8 bytes (2 NVS entries) instead of ~100 bytes (4 entries), reducing
// flash wear by ~50% per write operation. Both FCntUp and FCntDown are saved
// to preserve downlink replay protection across reboots.
static bool lorawan_session_save_fcnt_nvs(void)
{
    MibRequestConfirm_t mib_up, mib_down;
    mib_up.Type = MIB_UPLINK_COUNTER;
    mib_down.Type = MIB_DOWNLINK_COUNTER;
    if (LoRaMacMibGetRequestConfirm(&mib_up) != LORAMAC_STATUS_OK) return false;
    if (LoRaMacMibGetRequestConfirm(&mib_down) != LORAMAC_STATUS_OK) return false;

    nvs_handle_t handle;
    if (nvs_open(LORAWAN_SESSION_NVS_NS, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_u32(handle, "fcnt", mib_up.Param.UpLinkCounter);
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, "fcnt_dn", mib_down.Param.DownLinkCounter);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return (err == ESP_OK);
}

void lorawan_port_clear_session(void)
{
    s_session_clear_seq++;
    lorawan_session_clear_nvs();
    s_last_uplink.valid = false;
    s_last_downlink.valid = false;
    s_session_save_counter = 0;
    s_tx_pending = false;
    s_tx_pending_since_ms = 0;
    s_last_tx_busy_log_ms = 0;
    s_join_pending = false;

    if (!s_inited) {
        ESP_LOGI(TAG, "Session cleared (NVS only)");
        return;
    }

    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_NETWORK_JOINED;
    mib.Param.IsNetworkJoined = false;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_UPLINK_COUNTER;
    mib.Param.UpLinkCounter = 0;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_DOWNLINK_COUNTER;
    mib.Param.DownLinkCounter = 0;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    // Clear any lingering class/radio state so the next OTAA join starts from a
    // deterministic Class A baseline instead of inheriting a previous session.
    s_class_b_state = CLASS_B_IDLE;
    (void)lorawan_port_set_class(CLASS_A);
    lorawan_reset_duty_cycle_backoff("session clear");

    ESP_LOGI(TAG, "Session cleared");
}

static void on_mcps_confirm(McpsConfirm_t *mcpsConfirm)
{
    if (!mcpsConfirm) return;
    bool ok = (mcpsConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK);
    s_tx_pending = false;
    s_tx_pending_since_ms = 0;
    s_last_uplink.valid = true;
    s_last_uplink.success = ok;
    s_last_uplink.dr = mcpsConfirm->Datarate;
    s_last_uplink.tx_power = mcpsConfirm->TxPower;
    s_last_uplink.retries = mcpsConfirm->NbRetries;
    s_last_uplink.counter = mcpsConfirm->UpLinkCounter;
    s_last_uplink.channel = mcpsConfirm->Channel;
    s_last_uplink.toa_ms = (uint32_t)mcpsConfirm->TxTimeOnAir;
    if (s_uplink_cb) {
        int16_t rssi = INT16_MIN;
        s_uplink_cb(ok, mcpsConfirm->Datarate, rssi);
    }

    if (s_session_store == LORAWAN_SESSION_STORE_NVS && ok) {
        uint16_t every = s_session_save_every ? s_session_save_every : 1;
        if (++s_session_save_counter >= every) {
            s_session_save_counter = 0;
            (void)lorawan_session_save_nvs();
        } else {
            // Counter-only save between full saves (reduces flash wear).
            (void)lorawan_session_save_fcnt_nvs();
        }
    }
}

static void on_mcps_indication(McpsIndication_t *mcpsIndication)
{
    if (!mcpsIndication) return;
    if (mcpsIndication->Status != LORAMAC_EVENT_INFO_STATUS_OK) return;
    s_last_downlink.valid = true;
    s_last_downlink.port = mcpsIndication->Port;
    s_last_downlink.len = mcpsIndication->BufferSize;
    s_last_downlink.dr = mcpsIndication->RxDatarate;
    s_last_downlink.rssi = mcpsIndication->Rssi;
    s_last_downlink.snr = mcpsIndication->Snr;
    s_last_downlink.slot = mcpsIndication->RxSlot;
    s_last_downlink.ack = mcpsIndication->AckReceived;
    s_last_downlink.pending = (mcpsIndication->FramePending != 0);
    s_last_downlink.counter = mcpsIndication->DownLinkCounter;
    if (!s_downlink_cb) return;

    if (!mcpsIndication->RxData || mcpsIndication->BufferSize == 0) {
        s_downlink_cb(mcpsIndication->Port, NULL, 0,
                      mcpsIndication->Rssi, mcpsIndication->Snr);
        return;
    }

    s_downlink_cb(mcpsIndication->Port,
                  mcpsIndication->Buffer,
                  mcpsIndication->BufferSize,
                  mcpsIndication->Rssi,
                  mcpsIndication->Snr);

    // Re-apply default EU868 channels if requested (network may send NewChannelReq).
    lorawan_apply_default_channels("downlink");
}
static void on_mlme_confirm(MlmeConfirm_t *mlmeConfirm)
{
    if (!mlmeConfirm) return;
    if (mlmeConfirm->MlmeRequest == MLME_JOIN) {
        bool ok = (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK);
        s_join_pending = false;
        if (!ok && lorawan_port_is_joined()) {
            ESP_LOGW(TAG, "Ignoring stale MLME_JOIN confirm status=%u after joined=1",
                     (unsigned)mlmeConfirm->Status);
            return;
        }
        ESP_LOGI(TAG, "MLME_JOIN confirm: status=%u", (unsigned)mlmeConfirm->Status);
        if (s_join_cb) s_join_cb(ok);
        if (ok && s_session_store == LORAWAN_SESSION_STORE_NVS) {
            s_session_save_counter = 0;
            (void)lorawan_session_save_nvs();
        }
        if (ok) {
            lorawan_apply_default_channels("join");
            (void)lorawan_apply_tx_datarate_if_needed("join");
        }
        if (ok && s_target_class == CLASS_B) {
            (void)lorawan_port_start_class_b(s_class_b_ping_periodicity);
        }
        return;
    }

    if (mlmeConfirm->MlmeRequest == MLME_DEVICE_TIME) {
        if (s_class_b_state == CLASS_B_WAIT_DEVICE_TIME) {
            if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
                s_class_b_state = CLASS_B_WAIT_BEACON_ACQ;
                MlmeReq_t req = {0};
                req.Type = MLME_BEACON_ACQUISITION;
                if (LoRaMacMlmeRequest(&req) != LORAMAC_STATUS_OK) {
                    s_class_b_state = CLASS_B_IDLE;
                }
            } else {
                ESP_LOGW(TAG, "Class B: device time failed (%u)", (unsigned)mlmeConfirm->Status);
                s_class_b_state = CLASS_B_IDLE;
            }
        }
        return;
    }

    if (mlmeConfirm->MlmeRequest == MLME_BEACON_ACQUISITION) {
        if (s_class_b_state == CLASS_B_WAIT_BEACON_ACQ) {
            if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
                s_class_b_state = CLASS_B_WAIT_PING_SLOT;
                MlmeReq_t req = {0};
                req.Type = MLME_PING_SLOT_INFO;
                req.Req.PingSlotInfo.PingSlot.Fields.Periodicity = s_class_b_ping_periodicity;
                if (LoRaMacMlmeRequest(&req) != LORAMAC_STATUS_OK) {
                    s_class_b_state = CLASS_B_IDLE;
                }
            } else {
                ESP_LOGW(TAG, "Class B: beacon acquisition failed (%u)", (unsigned)mlmeConfirm->Status);
                s_class_b_state = CLASS_B_IDLE;
            }
        }
        return;
    }

    if (mlmeConfirm->MlmeRequest == MLME_PING_SLOT_INFO) {
        if (s_class_b_state == CLASS_B_WAIT_PING_SLOT) {
            if (mlmeConfirm->Status == LORAMAC_EVENT_INFO_STATUS_OK) {
                if (lorawan_port_set_class(CLASS_B)) {
                    s_class_b_state = CLASS_B_ACTIVE;
                    ESP_LOGI(TAG, "Class B active (periodicity=%u)", (unsigned)s_class_b_ping_periodicity);
                } else {
                    s_class_b_state = CLASS_B_IDLE;
                }
            } else {
                ESP_LOGW(TAG, "Class B: ping slot info failed (%u)", (unsigned)mlmeConfirm->Status);
                s_class_b_state = CLASS_B_IDLE;
            }
        }
        return;
    }
}
static void on_mlme_indication(MlmeIndication_t *mlmeIndication)
{
    if (!mlmeIndication) return;
    if (mlmeIndication->MlmeIndication == MLME_BEACON_LOST) {
        ESP_LOGW(TAG, "Class B: beacon lost, switching to Class A");
        (void)lorawan_port_stop_class_b();
        return;
    }
    if (mlmeIndication->MlmeIndication == MLME_BEACON) {
        ESP_LOGI(TAG, "Class B: beacon status=%u", (unsigned)mlmeIndication->Status);
    }
}

static uint8_t cb_battery_level(void) { return 255; }
static float cb_temperature_level(void) { return 25.0f; }

static uint8_t s_dev_eui[8] = { 0 };
static uint8_t s_app_eui[8] = { 0 };
static uint8_t s_app_key[16] = { 0 };

static int8_t get_default_datarate(void)
{
    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_CHANNELS_DATARATE;
    if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
        return mib.Param.ChannelsDatarate;
    }
    return 0;
}

bool lorawan_port_init(const lorawan_cfg_t *cfg)
{
    s_inited = false;
    s_join_pending = false;
    LoRaMacRegion_t region = LORAMAC_REGION_EU868;
    DeviceClass_t device_class = CLASS_A;
    bool adr_enabled = false;
    int8_t join_datarate = -1;
    uint32_t rx2_frequency = 0;
    int8_t rx2_datarate = -1;
    int8_t tx_datarate = -1;
    s_session_store = LORAWAN_SESSION_STORE_NONE;
    s_session_save_every = 0;
    s_session_save_counter = 0;
    if (cfg) region = cfg->region;
    if (cfg) device_class = cfg->device_class;
    if (cfg) adr_enabled = cfg->adr_enabled;
    if (cfg) join_datarate = cfg->join_datarate;
    if (cfg) rx2_frequency = cfg->rx2_frequency;
    if (cfg) rx2_datarate = cfg->rx2_datarate;
    if (cfg) tx_datarate = cfg->tx_datarate;
    if (cfg) s_session_store = cfg->session_store;
    if (cfg) s_session_save_every = cfg->session_save_every;
    if (cfg) s_class_b_ping_periodicity = cfg->class_b_ping_periodicity;
    s_uplink_confirm_every = 0;
    s_uplink_counter = 0;
    s_uplink_confirm_next = -1;
    s_tx_pending = false;
    s_tx_pending_since_ms = 0;
    s_last_tx_busy_log_ms = 0;
    s_region = region;
    s_channel_block=cfg&&(region==LORAMAC_REGION_US915||region==LORAMAC_REGION_AU915)?cfg->channel_block:0;
    s_as_group=cfg&&cfg->as923_group?cfg->as923_group:1;s_initial_dwell=cfg?cfg->uplink_dwell:0;
    if(region==LORAMAC_REGION_AS923&&!RegionAS923SetGroup(s_as_group))return false;
    s_target_class = device_class;
    s_class = (device_class == CLASS_B) ? CLASS_A : device_class;
    s_class_b_state = CLASS_B_IDLE;
    s_tx_datarate = tx_datarate;

    memset(&s_mac_primitives, 0, sizeof(s_mac_primitives));
    s_mac_primitives.MacMcpsConfirm = on_mcps_confirm;
    s_mac_primitives.MacMcpsIndication = on_mcps_indication;
    s_mac_primitives.MacMlmeConfirm = on_mlme_confirm;
    s_mac_primitives.MacMlmeIndication = on_mlme_indication;

    memset(&s_mac_callbacks, 0, sizeof(s_mac_callbacks));
    s_mac_callbacks.GetBatteryLevel = cb_battery_level;
    s_mac_callbacks.GetTemperatureLevel = cb_temperature_level;

    LoRaMacStatus_t st = LoRaMacInitialization(&s_mac_primitives, &s_mac_callbacks, region);
    if (st != LORAMAC_STATUS_OK) {
        ESP_LOGE(TAG, "LoRaMacInitialization failed: %d", (int)st);
        return false;
    }

    if(region==LORAMAC_REGION_AU915||region==LORAMAC_REGION_AS923)LoRaMacSetInitialDwell(s_initial_dwell);

    // Ensure public network syncword (LoRaWAN)
    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_PUBLIC_NETWORK;
    mib.Param.EnablePublicNetwork = true;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    mib.Type = MIB_ADR;
    mib.Param.AdrEnable = adr_enabled;
    (void)LoRaMacMibSetRequestConfirm(&mib);

    lorawan_apply_default_channels("init");
    if(!lorawan_us_channels())return false;

    if (join_datarate >= 0) {
        mib.Type = MIB_CHANNELS_DATARATE;
        mib.Param.ChannelsDatarate = (uint8_t)join_datarate;
        LoRaMacStatus_t dr_st = LoRaMacMibSetRequestConfirm(&mib);
        if (dr_st != LORAMAC_STATUS_OK) {
            ESP_LOGW(TAG, "Join DR override failed: %d", (int)dr_st);
        }
    }

    if (rx2_frequency > 0 || rx2_datarate >= 0) {
        mib.Type = MIB_RX2_CHANNEL;
        if (LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) {
            if (rx2_frequency > 0) {
                mib.Param.Rx2Channel.Frequency = rx2_frequency;
            }
            if (rx2_datarate >= 0) {
                mib.Param.Rx2Channel.Datarate = (uint8_t)rx2_datarate;
            }
            LoRaMacStatus_t rx2_st = LoRaMacMibSetRequestConfirm(&mib);
            if (rx2_st != LORAMAC_STATUS_OK) {
                ESP_LOGW(TAG, "RX2 override failed: %d", (int)rx2_st);
            }
        } else {
            ESP_LOGW(TAG, "RX2 override read failed");
        }
    }

    (void)lorawan_port_set_class(s_class);

    s_inited = true;
    if (s_session_save_every == 0) {
        s_session_save_every = 1;
    }
    bool restored = false;
    if (s_session_store == LORAWAN_SESSION_STORE_NVS) {
        restored = lorawan_session_restore_nvs();
        if (restored && s_target_class == CLASS_B) {
            (void)lorawan_port_start_class_b(s_class_b_ping_periodicity);
        }
    }
    lorawan_apply_default_channels("post-restore");
    if (restored) {
        (void)lorawan_port_set_adr(adr_enabled);
    }
    (void)lorawan_apply_tx_datarate_if_needed("init");
    ESP_LOGI(TAG, "LoRaMac initialized (region=%d)", (int)region);
    return true;
}

bool lorawan_port_is_inited(void)
{
    return s_inited;
}

void lorawan_port_set_join_callback(lorawan_join_cb_t cb)
{
    s_join_cb = cb;
}

void lorawan_port_set_uplink_callback(lorawan_uplink_cb_t cb)
{
    s_uplink_cb = cb;
}

void lorawan_port_set_downlink_callback(lorawan_downlink_cb_t cb)
{
    s_downlink_cb = cb;
}

bool lorawan_port_join_otaa(const uint8_t dev_eui[8], const uint8_t app_eui[8], const uint8_t app_key[16], uint8_t nb_trials)
{
    if (!s_inited) return false;
    if (!dev_eui || !app_eui || !app_key) return false;
    memcpy(s_dev_eui, dev_eui, 8);
    memcpy(s_app_eui, app_eui, 8);
    memcpy(s_app_key, app_key, 16);

    MlmeReq_t mlmeReq = {0};
    mlmeReq.Type = MLME_JOIN;
    mlmeReq.Req.Join.DevEui = s_dev_eui;
    mlmeReq.Req.Join.AppEui = s_app_eui;
    mlmeReq.Req.Join.AppKey = s_app_key;
    mlmeReq.Req.Join.NbTrials = (nb_trials == 0) ? 3 : nb_trials;

    LoRaMacStatus_t st = LoRaMacMlmeRequest(&mlmeReq);
    s_join_pending = (st == LORAMAC_STATUS_OK);
    ESP_LOGI(TAG, "LoRaMacMlmeRequest(MLME_JOIN) => %d", (int)st);
    return st == LORAMAC_STATUS_OK;
}

bool lorawan_port_join_abp(uint32_t dev_addr, const uint8_t nwk_skey[16], const uint8_t app_skey[16])
{
    if (!s_inited) return false;
    if (!nwk_skey || !app_skey) return false;

    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_DEV_ADDR;
    mib.Param.DevAddr = dev_addr;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        ESP_LOGW(TAG, "ABP: set devaddr failed");
        return false;
    }

    mib.Type = MIB_NWK_SKEY;
    mib.Param.NwkSKey = (uint8_t *)nwk_skey;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        ESP_LOGW(TAG, "ABP: set nwk_skey failed");
        return false;
    }

    mib.Type = MIB_APP_SKEY;
    mib.Param.AppSKey = (uint8_t *)app_skey;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        ESP_LOGW(TAG, "ABP: set app_skey failed");
        return false;
    }

    mib.Type = MIB_NETWORK_JOINED;
    mib.Param.IsNetworkJoined = true;
    if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        ESP_LOGW(TAG, "ABP: set network joined failed");
        return false;
    }

    if (s_join_cb) s_join_cb(true);
    if (s_session_store == LORAWAN_SESSION_STORE_NVS) {
        s_session_save_counter = 0;
        (void)lorawan_session_save_nvs();
    }
    (void)lorawan_apply_tx_datarate_if_needed("abp");
    if (s_target_class == CLASS_B) {
        (void)lorawan_port_start_class_b(s_class_b_ping_periodicity);
    }
    ESP_LOGI(TAG, "ABP activation OK (devaddr=0x%08X)", (unsigned)dev_addr);
    return true;
}

static bool lorawan_port_send_req(Mcps_t type, uint8_t port, const uint8_t *data, uint8_t len)
{
    if (!s_inited) return false;
    if (!lorawan_port_is_joined()) return false;
    if (len > 0 && !data) return false;

    uint32_t now_ms = esp_log_timestamp();
    uint32_t mac_state = LoRaMacState;
    if ((mac_state & (LORAMAC_TX_RUNNING | LORAMAC_TX_DELAYED | LORAMAC_TX_CONFIG | LORAMAC_CAD_RUNNING)) != 0) {
        if ((mac_state & (LORAMAC_TX_RUNNING | LORAMAC_TX_DELAYED)) != 0) {
            if (!s_tx_pending) {
                s_tx_pending = true;
                s_tx_pending_since_ms = now_ms;
            }
        }
        if ((now_ms - s_last_tx_busy_log_ms) > 2000) {
            uint32_t pending_ms = 0;
            if (s_tx_pending_since_ms != 0 && now_ms >= s_tx_pending_since_ms) {
                pending_ms = now_ms - s_tx_pending_since_ms;
            }
            if (pending_ms > 0) {
                ESP_LOGW(TAG, "Uplink delayed: mac_state=0x%08X pending_ms=%u",
                         (unsigned)mac_state, (unsigned)pending_ms);
            } else {
                ESP_LOGW(TAG, "Uplink delayed: mac_state=0x%08X", (unsigned)mac_state);
            }
            s_last_tx_busy_log_ms = now_ms;
        }
        return false;
    }

    LoRaMacTxInfo_t txInfo = {0};
    LoRaMacStatus_t can_tx = LoRaMacQueryTxPossible(len, &txInfo);
    if (can_tx != LORAMAC_STATUS_OK) {
        ESP_LOGW(TAG, "LoRaMacQueryTxPossible failed: %d", (int)can_tx);
        return false;
    }

    McpsReq_t req = {0};
    req.Type = type;

    int8_t dr = get_default_datarate();
    if (s_tx_datarate >= 0) {
        bool adr = false;
        if (!(lorawan_port_get_adr(&adr) && adr)) {
            dr = s_tx_datarate;
        }
    }
    if (type == MCPS_CONFIRMED) {
        req.Req.Confirmed.fPort = port;
        req.Req.Confirmed.fBuffer = (void *)data;
        req.Req.Confirmed.fBufferSize = len;
        req.Req.Confirmed.Datarate = dr;
        req.Req.Confirmed.NbTrials = 3;
    } else {
        req.Req.Unconfirmed.fPort = port;
        req.Req.Unconfirmed.fBuffer = (void *)data;
        req.Req.Unconfirmed.fBufferSize = len;
        req.Req.Unconfirmed.Datarate = dr;
    }

    LoRaMacStatus_t st = LoRaMacMcpsRequest(&req);
    if (st != LORAMAC_STATUS_OK) {
        if (st == LORAMAC_STATUS_BUSY) {
            if ((now_ms - s_last_tx_busy_log_ms) > 2000) {
                uint32_t pending_ms = 0;
                if (s_tx_pending_since_ms != 0 && now_ms >= s_tx_pending_since_ms) {
                    pending_ms = now_ms - s_tx_pending_since_ms;
                }
                if (pending_ms > 0) {
                    ESP_LOGW(TAG, "Uplink delayed: mac_state=0x%08X pending_ms=%u",
                             (unsigned)LoRaMacState, (unsigned)pending_ms);
                } else {
                    ESP_LOGW(TAG, "Uplink delayed: mac_state=0x%08X", (unsigned)LoRaMacState);
                }
                s_last_tx_busy_log_ms = now_ms;
            }
            if ((LoRaMacState & (LORAMAC_TX_RUNNING | LORAMAC_TX_DELAYED)) != 0) {
                if (!s_tx_pending) {
                    s_tx_pending = true;
                    s_tx_pending_since_ms = now_ms;
                }
            }
        } else {
            ESP_LOGW(TAG, "LoRaMacMcpsRequest failed: %d", (int)st);
        }
    } else {
        s_tx_pending = true;
        s_tx_pending_since_ms = now_ms;
    }
    return st == LORAMAC_STATUS_OK;
}

bool lorawan_port_send_unconfirmed(uint8_t port, const uint8_t *data, uint8_t len)
{
    return lorawan_port_send_req(MCPS_UNCONFIRMED, port, data, len);
}

bool lorawan_port_send_confirmed(uint8_t port, const uint8_t *data, uint8_t len)
{
    return lorawan_port_send_req(MCPS_CONFIRMED, port, data, len);
}

bool lorawan_port_send_ex(uint8_t port, const uint8_t *data, uint8_t len, bool confirmed)
{
    return lorawan_port_send_req(confirmed ? MCPS_CONFIRMED : MCPS_UNCONFIRMED,
                                 port, data, len);
}

bool lorawan_port_is_tx_pending(void)
{
    if (!s_inited) return false;
    if ((LoRaMacState & (LORAMAC_TX_RUNNING | LORAMAC_TX_DELAYED |
                         LORAMAC_TX_CONFIG | LORAMAC_CAD_RUNNING)) != 0) {
        if (!s_tx_pending) {
            s_tx_pending = true;
            s_tx_pending_since_ms = esp_log_timestamp();
        }
        return true;
    }
    if (s_tx_pending) {
        s_tx_pending = false;
        s_tx_pending_since_ms = 0;
    }
    return false;
}

bool lorawan_port_can_tx(uint8_t len)
{
    if (!s_inited) return false;
    if (!lorawan_port_is_joined()) return false;

    uint32_t mac_state = LoRaMacState;
    if ((mac_state & (LORAMAC_TX_RUNNING | LORAMAC_TX_DELAYED |
                      LORAMAC_TX_CONFIG | LORAMAC_CAD_RUNNING)) != 0) {
        return false;
    }

    LoRaMacTxInfo_t txInfo = {0};
    return LoRaMacQueryTxPossible(len, &txInfo) == LORAMAC_STATUS_OK;
}

void lorawan_port_set_uplink_confirm_every(uint16_t confirm_every)
{
    s_uplink_confirm_every = confirm_every;
}

uint16_t lorawan_port_get_uplink_confirm_every(void)
{
    return s_uplink_confirm_every;
}

void lorawan_port_set_uplink_confirm_next(int8_t confirmed_next)
{
    if (confirmed_next < -1) confirmed_next = -1;
    if (confirmed_next > 1) confirmed_next = 1;
    s_uplink_confirm_next = confirmed_next;
}

int8_t lorawan_port_get_uplink_confirm_next(void)
{
    return s_uplink_confirm_next;
}

bool lorawan_port_send_auto(uint8_t port, const uint8_t *data, uint8_t len)
{
    bool confirmed = false;
    if (s_uplink_confirm_next >= 0) {
        confirmed = (s_uplink_confirm_next == 1);
    } else if (s_uplink_confirm_every == 1) {
        confirmed = true;
    } else if (s_uplink_confirm_every > 1) {
        confirmed = ((s_uplink_counter + 1) % s_uplink_confirm_every) == 0;
    }

    bool ok = lorawan_port_send_req(confirmed ? MCPS_CONFIRMED : MCPS_UNCONFIRMED,
                                    port, data, len);
    if (ok) {
        if (s_uplink_confirm_next >= 0) s_uplink_confirm_next = -1;
        s_uplink_counter++;
    }
    return ok;
}

bool lorawan_port_is_joined(void)
{
    if (!s_inited) return false;
    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_NETWORK_JOINED;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;
    return mib.Param.IsNetworkJoined;
}

bool lorawan_port_is_join_pending(void)
{
    return s_join_pending;
}

uint32_t lorawan_port_get_session_clear_seq(void)
{
    return s_session_clear_seq;
}

void lorawan_port_process(void)
{
    if (!s_inited) return;
    // Keep the periodic poll as a fallback for missed DIO1 edges.
    // Bogus 0xFFFF reads are ignored inside RadioIrqProcessImpl.
    if (Radio.IrqProcess) {
        Radio.IrqProcess();
    }
}

bool lorawan_port_set_class(DeviceClass_t device_class)
{
    if (!s_inited) return false;
    MibRequestConfirm_t current = {0};
    current.Type = MIB_DEVICE_CLASS;
    if (LoRaMacMibGetRequestConfirm(&current) == LORAMAC_STATUS_OK &&
        current.Param.Class == device_class) {
        s_class = device_class;
        return true;
    }
    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_DEVICE_CLASS;
    mib.Param.Class = device_class;
    LoRaMacStatus_t st = LoRaMacMibSetRequestConfirm(&mib);
    if (st != LORAMAC_STATUS_OK) {
        ESP_LOGW(TAG, "LoRaMacMibSetRequestConfirm(MIB_DEVICE_CLASS) failed: %d", (int)st);
        return false;
    }
    s_class = device_class;
    return true;
}

DeviceClass_t lorawan_port_get_class(void)
{
    if (!s_inited) return CLASS_A;
    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_DEVICE_CLASS;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
        return CLASS_A;
    }
    return mib.Param.Class;
}

bool lorawan_port_set_adr(bool enable)
{
    if (!s_inited) return false;
    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_ADR;
    mib.Param.AdrEnable = enable;
    LoRaMacStatus_t st = LoRaMacMibSetRequestConfirm(&mib);
    if (st == LORAMAC_STATUS_OK) {
        (void)lorawan_apply_tx_datarate_if_needed("adr");
    }
    return st == LORAMAC_STATUS_OK;
}

bool lorawan_port_get_adr(bool *enabled)
{
    if (!enabled || !s_inited) return false;
    MibRequestConfirm_t mib = {0};
    mib.Type = MIB_ADR;
    if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) return false;
    *enabled = mib.Param.AdrEnable;
    return true;
}

LoRaMacRegion_t lorawan_port_get_region(void)
{
    return s_region;
}

bool lorawan_port_set_tx_datarate(int8_t dr)
{
    if (dr < -1) dr = -1;
    s_tx_datarate = dr;
    if (dr < 0) return true;
    if (!s_inited) return true;

    bool adr = false;
    if (lorawan_port_get_adr(&adr) && adr) {
        ESP_LOGW(TAG, "TX DR override ignored (ADR on)");
        return false;
    }
    if (!lorawan_port_is_joined()) {
        ESP_LOGI(TAG, "TX DR override deferred until join");
        return true;
    }
    return lorawan_apply_tx_datarate_if_needed("set");
}

bool lorawan_port_get_tx_datarate(int8_t *dr)
{
    if (!dr) return false;
    *dr = s_tx_datarate;
    return true;
}

bool lorawan_port_sf_to_dr(LoRaMacRegion_t region, uint8_t sf, int8_t *dr)
{
    if (!dr) return false;
    switch (region) {
        case LORAMAC_REGION_US915:
        case LORAMAC_REGION_US915_HYBRID:
            switch (sf) {
                case 10: *dr = 0; return true;
                case 9: *dr = 1; return true;
                case 8: *dr = 2; return true;
                case 7: *dr = 3; return true;
                default: return false;
            }
        default:
            break;
    }

    switch (sf) {
        case 12: *dr = 0; return true;
        case 11: *dr = 1; return true;
        case 10: *dr = 2; return true;
        case 9: *dr = 3; return true;
        case 8: *dr = 4; return true;
        case 7: *dr = 5; return true;
        default: return false;
    }
}

bool lorawan_port_set_tx_sf(uint8_t sf)
{
    if (sf == 0) {
        return lorawan_port_set_tx_datarate(-1);
    }
    int8_t dr = -1;
    if (!lorawan_port_sf_to_dr(s_region, sf, &dr)) {
        ESP_LOGW(TAG, "TX SF %u not supported for region %d", (unsigned)sf, (int)s_region);
        return false;
    }
    return lorawan_port_set_tx_datarate(dr);
}

bool lorawan_port_start_class_b(uint8_t periodicity)
{
    if (!s_inited || !lorawan_port_is_joined()) return false;
    if (periodicity > 7) periodicity = 7;
    s_class_b_ping_periodicity = periodicity;
    if (s_class_b_state == CLASS_B_ACTIVE) return true;

    MlmeReq_t req = {0};
    req.Type = MLME_DEVICE_TIME;
    if (LoRaMacMlmeRequest(&req) != LORAMAC_STATUS_OK) {
        return false;
    }
    s_class_b_state = CLASS_B_WAIT_DEVICE_TIME;
    return true;
}

bool lorawan_port_stop_class_b(void)
{
    if (!s_inited) return false;
    LoRaMacClassBStop();
    s_class_b_state = CLASS_B_IDLE;
    return lorawan_port_set_class(CLASS_A);
}

bool lorawan_port_is_class_b_active(void)
{
    return s_class_b_state == CLASS_B_ACTIVE;
}

bool lorawan_port_tx_cw(uint32_t freq_hz, int8_t power_dbm, uint16_t duration_s)
{
    if (!s_inited) return false;
    Radio.SetModem(MODEM_LORA);
    Radio.SetTxContinuousWave(freq_hz, power_dbm, duration_s);
    return true;
}

void lorawan_port_tx_cw_stop(void)
{
    if (!s_inited) return;
    RadioStopTxContinuousWave();
}

bool lorawan_port_get_last_uplink(lorawan_uplink_info_t *out)
{
    if (!out) return false;
    *out = s_last_uplink;
    return s_last_uplink.valid;
}

bool lorawan_port_get_last_downlink(lorawan_downlink_info_t *out)
{
    if (!out) return false;
    *out = s_last_downlink;
    return s_last_downlink.valid;
}

LoRaMacRegion_t lorawan_port_region(unsigned r){
 static const LoRaMacRegion_t regions[]={LORAMAC_REGION_EU868,LORAMAC_REGION_US915,LORAMAC_REGION_AU915,LORAMAC_REGION_IN865,LORAMAC_REGION_KR920,LORAMAC_REGION_AS923,LORAMAC_REGION_AS923,LORAMAC_REGION_AS923,LORAMAC_REGION_AS923,LORAMAC_REGION_RU864};
 return r<TRACKER_REGION_COUNT?regions[r]:LORAMAC_REGION_EU868;
}
int lorawan_port_region_id(LoRaMacRegion_t region){for(unsigned i=0;i<TRACKER_REGION_COUNT;i++)if(lorawan_port_region(i)==region)return region==LORAMAC_REGION_AS923?4+s_as_group:(int)i;return -1;}
int8_t lorawan_port_baseline_power(void){return LoRaMacBaselinePower();}
unsigned lorawan_port_uplink_dwell(void){return LoRaMacUplinkDwell();}
bool lorawan_port_get_dr_params(uint8_t dr,uint8_t *sf,uint16_t *bw_khz,bool *fsk){
 uint8_t value=0;uint16_t bw=0;int id=lorawan_port_region_id(s_region);if(fsk)*fsk=false;
 bool ok=id>=0&&tracker_region_rate(id,dr,&value,&bw);
 if(sf)*sf=value;
 if(bw_khz)*bw_khz=bw;
 if(fsk)*fsk=!ok&&dr==7;
 return ok;
}

bool lorawan_port_reset_adr_power(void) {
    if(!s_inited||lorawan_port_is_tx_pending()||lorawan_port_is_join_pending())return false;
    MibRequestConfirm_t mib={.Type=MIB_CHANNELS_TX_POWER};mib.Param.ChannelsTxPower=0;
    if(LoRaMacMibSetRequestConfirm(&mib)!=LORAMAC_STATUS_OK)return false;
    LoRaMacResetAdrCounter();
    return !lorawan_port_is_joined()||lorawan_session_save_nvs();
}
