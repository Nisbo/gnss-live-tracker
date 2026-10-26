/* GNSS Live Tracker V2 modifications, 2026. Based on HTIT Tracker v1.2; see NOTICE and docs/UPSTREAM.md. */
#include "radio.h"

#include <string.h>
#include <math.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "sx1262.h"
#include "timer.h"
#include "utilities.h"

static RadioEvents_t *s_events = NULL;
static RadioState_t s_state = RF_IDLE;
static RadioModems_t s_modem = MODEM_LORA;
static uint32_t s_freq_hz = 0;
static esp_timer_handle_t s_cw_timer = NULL;

static const char *TAG = "radio";

// LoRa config snapshot (used for TimeOnAir/RSSI decisions)
static uint8_t s_bw = 0;   // LoRaMac: 0=125k,1=250k,2=500k
static uint8_t s_sf = 7;   // 6..12
static uint8_t s_cr = 1;   // 1..4
static uint16_t s_preamble = 8;
static bool s_fix_len = false;
static bool s_crc_on = true;
static bool s_iq_inverted = false;
static bool s_rx_cont = false;
static uint8_t s_max_payload = 255;
static uint16_t s_rx_symb_timeout = 0;

static uint8_t s_rx_buf[255];

// LoRa syncword regs (SX126x)
static const uint16_t REG_LORA_SYNCWORD_MSB = 0x0740;

static uint8_t bw_to_sx126x(uint32_t bw)
{
    // LoRaMac bw: 0,1,2
    // SX126x bw enum: 0x04=125k, 0x05=250k, 0x06=500k
    switch (bw) {
        case 0: return 0x04;
        case 1: return 0x05;
        case 2: return 0x06;
        default: return 0x04;
    }
}

static uint32_t bw_to_hz(uint8_t bw)
{
    switch (bw) {
        case 0: return 125000;
        case 1: return 250000;
        case 2: return 500000;
        default: return 125000;
    }
}

static bool ldro_should_enable(uint8_t sf, uint8_t bw)
{
    // Standard heuristic: enable LDRO for long symbol durations (e.g., SF11/SF12 @125k)
    if (bw == 0 && (sf == 11 || sf == 12)) return true;
    return false;
}

static void RadioIrqProcessImpl(void);
static void RadioRxImpl(uint32_t timeout);
static void RadioSleepImpl(void);
static void RadioStopTxContinuousWaveImpl(void);
static void RadioCwTimerCb(void *arg)
{
    (void)arg;
    RadioStopTxContinuousWaveImpl();
}

static int RadioInitImpl(RadioEvents_t *events)
{
    s_events = events;
    s_state = RF_IDLE;
    s_modem = MODEM_LORA;

    if (!sx1262_init(&(sx1262_cfg_t){ .spi_clock_hz = 8000000 })) {
        return -1;
    }

    // DIO1 -> sx1262 internal task -> this callback
    sx1262_set_dio1_callback(RadioIrqProcessImpl);

    // Reasonable defaults
    sx1262_set_standby(SX126X_STANDBY_RC);
    sx1262_set_packet_type(SX126X_PACKET_TYPE_LORA);
    sx1262_set_buffer_base_address(0x00, 0x00);

    // Public network by default (LoRaWAN uses 0x34/0x44 on SX126x)
    uint8_t pub_sw[2] = { 0x34, 0x44 };
    (void)sx1262_write_register(REG_LORA_SYNCWORD_MSB, pub_sw, sizeof(pub_sw));

    return 0;
}

static RadioState_t RadioGetStatusImpl(void)
{
    return s_state;
}

static void RadioSetModemImpl(RadioModems_t modem)
{
    s_modem = modem;
    if (modem == MODEM_LORA) {
        (void)sx1262_set_packet_type(SX126X_PACKET_TYPE_LORA);
    } else {
        (void)sx1262_set_packet_type(SX126X_PACKET_TYPE_GFSK);
    }
}

static void RadioSetChannelImpl(uint32_t freq)
{
    s_freq_hz = freq;
    (void)sx1262_set_rf_frequency(freq);
}

static bool RadioIsChannelFreeImpl(RadioModems_t modem, uint32_t freq, int16_t rssiThresh, uint32_t maxCarrierSenseTime)
{
    if (modem != MODEM_LORA) return true;

    RadioSetModemImpl(modem);
    RadioSetChannelImpl(freq);
    RadioRxImpl(0);

    // Let RSSI settle a bit (Heltec does a ~1ms delay here).
    esp_rom_delay_us(1000);

    bool free = true;
    TimerTime_t start = TimerGetCurrentTime();
    while (TimerGetElapsedTime(start) < maxCarrierSenseTime) {
        int16_t rssi = -127;
        if(!sx1262_get_rssi_inst(&rssi)){free=false;break;}
        if (rssi > rssiThresh && rssi != 0) {
            free = false;
            break;
        }
    }

    RadioSleepImpl();
    return free;
}

static uint32_t RadioRandomImpl(void)
{
    // Use TRNG to avoid repeated DevNonce/JoinNonce collisions across boots.
    return esp_random();
}

static void RadioSetRxConfigImpl(RadioModems_t modem, uint32_t bandwidth, uint32_t datarate, uint8_t coderate,
                                 uint32_t bandwidthAfc, uint16_t preambleLen, uint16_t symbTimeout, bool fixLen,
                                 uint8_t payloadLen, bool crcOn, bool freqHopOn, uint8_t hopPeriod,
                                 bool iqInverted, bool rxContinuous)
{
    (void)bandwidthAfc;
    (void)freqHopOn;
    (void)hopPeriod;

    if (modem != MODEM_LORA) return;

    s_bw = (uint8_t)bandwidth;
    s_sf = (uint8_t)datarate;
    s_cr = coderate;
    s_preamble = preambleLen;
    s_fix_len = fixLen;
    s_crc_on = crcOn;
    s_iq_inverted = iqInverted;
    s_rx_cont = rxContinuous;
    s_rx_symb_timeout = symbTimeout;
    s_max_payload = payloadLen ? payloadLen : s_max_payload;

    uint8_t sx_bw = bw_to_sx126x(bandwidth);
    uint8_t ldro = ldro_should_enable(s_sf, s_bw) ? 0x01 : 0x00;

    (void)sx1262_set_packet_type(SX126X_PACKET_TYPE_LORA);
    (void)sx1262_set_modulation_params_lora((uint8_t)s_sf, sx_bw, s_cr, ldro);

    uint8_t header = fixLen ? 0x01 : 0x00; // 0: explicit, 1: implicit
    uint8_t crc = crcOn ? 0x01 : 0x00;
    uint8_t inv = iqInverted ? 0x01 : 0x00;
    (void)sx1262_set_packet_params_lora(preambleLen, header, s_max_payload, crc, inv);
}

static void RadioSetTxConfigImpl(RadioModems_t modem, int8_t power, uint32_t fdev, uint32_t bandwidth,
                                 uint32_t datarate, uint8_t coderate, uint16_t preambleLen, bool fixLen,
                                 bool crcOn, bool freqHopOn, uint8_t hopPeriod, bool iqInverted, uint32_t timeout)
{
    (void)fdev;
    (void)freqHopOn;
    (void)hopPeriod;
    (void)timeout;

    if (modem != MODEM_LORA) return;

    s_bw = (uint8_t)bandwidth;
    s_sf = (uint8_t)datarate;
    s_cr = coderate;
    s_preamble = preambleLen;
    s_fix_len = fixLen;
    s_crc_on = crcOn;
    s_iq_inverted = iqInverted;

    uint8_t sx_bw = bw_to_sx126x(bandwidth);
    uint8_t ldro = ldro_should_enable(s_sf, s_bw) ? 0x01 : 0x00;

    (void)sx1262_set_packet_type(SX126X_PACKET_TYPE_LORA);
    (void)sx1262_set_modulation_params_lora((uint8_t)s_sf, sx_bw, s_cr, ldro);

    uint8_t header = fixLen ? 0x01 : 0x00;
    uint8_t crc = crcOn ? 0x01 : 0x00;
    uint8_t inv = iqInverted ? 0x01 : 0x00;
    (void)sx1262_set_packet_params_lora(preambleLen, header, 0xFF, crc, inv);

    // PA config (SX1262: deviceSel=0x00, PA_BOOST path)
    (void)sx1262_set_pa_config(0x04, 0x07, 0x00, 0x01);
    (void)sx1262_set_ocp(SX126X_OCP_160MA);
    (void)sx1262_set_tx_params(power, SX126X_RAMP_3400_US);
}

static bool RadioCheckRfFrequencyImpl(uint32_t frequency)
{
    // Basic sanity; assume region already handles legality.
    return (frequency > 100000000UL) && (frequency < 1000000000UL);
}

static uint32_t RadioTimeOnAirImpl(RadioModems_t modem, uint8_t pktLen)
{
    if (modem != MODEM_LORA) return 0;

    const uint32_t bw = bw_to_hz(s_bw);
    const double ts = (double)(1UL << s_sf) / (double)bw;

    const double t_preamble = ( (double)s_preamble + 4.25 ) * ts;
    const int ih = s_fix_len ? 1 : 0;
    const int crc = s_crc_on ? 1 : 0;
    const int de = ldro_should_enable(s_sf, s_bw) ? 1 : 0;

    double tmp = (8.0 * pktLen - 4.0 * s_sf + 28.0 + 16.0 * crc - 20.0 * ih);
    double denom = 4.0 * (s_sf - 2.0 * de);
    double n_payload = 8.0;
    if (tmp > 0 && denom > 0) {
        n_payload += ceil(tmp / denom) * (double)(s_cr + 4);
    }
    const double t_payload = n_payload * ts;
    const double t_ms = (t_preamble + t_payload) * 1000.0;
    return (uint32_t)(t_ms + 0.999);
}

static void RadioSendImpl(uint8_t *buffer, uint8_t size)
{
    if (s_modem != MODEM_LORA) return;

    s_state = RF_TX_RUNNING;
    ESP_LOGI(TAG, "RadioSend: size=%u freq=%lu sf=%u bw=%u state=%d",
             (unsigned)size, (unsigned long)s_freq_hz, (unsigned)s_sf,
             (unsigned)s_bw, (int)s_state);

    (void)sx1262_set_standby(SX126X_STANDBY_RC);
    (void)sx1262_set_buffer_base_address(0x00, 0x00);
    if (!sx1262_write_buffer(0x00, buffer, size)) {
        ESP_LOGE(TAG, "TX buffer SPI write failed");
        if (s_events && s_events->TxTimeout) s_events->TxTimeout();
        return;
    }
    uint8_t readback[255];
    uint8_t syncword[2]={0};
    uint8_t device_errors[2]={0xFF,0xFF};
    // Semtech documents XOSC_START_ERR as a possible TCXO startup flag.
    // Clear only that isolated flag, then actually start XOSC and check again.
    if (sx1262_cmd_read(0x17, device_errors, 2) && device_errors[0]==0 && device_errors[1]==0x20) {
        const uint8_t clear[2]={0,0};
        (void)sx1262_cmd_write(0x07,clear,2);
        (void)sx1262_set_standby(SX126X_STANDBY_XOSC);
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_LOGI(TAG,"TCXO startup flag cleared; checking active XOSC");
    }
    if (!sx1262_read_buffer(0x00, readback, size) || memcmp(readback, buffer, size)!=0 ||
        !sx1262_read_register(REG_LORA_SYNCWORD_MSB, syncword, 2) ||
        !sx1262_cmd_read(0x17, device_errors, 2) ||
        syncword[0]!=0x34 || syncword[1]!=0x44 || device_errors[0] || device_errors[1]) {
        ESP_LOGE(TAG, "TX verification failed: sync=%02x%02x errors=%02x%02x",
                 syncword[0],syncword[1],device_errors[0],device_errors[1]);
        if (s_events && s_events->TxTimeout) s_events->TxTimeout();
        return;
    }
    ESP_LOGI(TAG,"TX verified: FIFO matches, public sync=3444, errors=0000, CR=4/%u CRC=%u IQ=%u LDRO=%u",
             (unsigned)(s_cr+4), s_crc_on, s_iq_inverted, ldro_should_enable(s_sf,s_bw));
    // A join request contains EUIs, nonce and MIC, never the secret AppKey.
    if (size==23 && buffer[0]==0) {
        char frame[47];
        const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<size;i++) {frame[2*i]=hex[buffer[i]>>4];frame[2*i+1]=hex[buffer[i]&15];}
        frame[46]=0;
        ESP_LOGI(TAG,"JOIN_FRAME=%s",frame);
    }

    // Update payload length for TX
    uint8_t header = s_fix_len ? 0x01 : 0x00;
    uint8_t crc = s_crc_on ? 0x01 : 0x00;
    uint8_t inv = s_iq_inverted ? 0x01 : 0x00;
    (void)sx1262_set_packet_params_lora(s_preamble, header, size, crc, inv);

    const uint16_t irq = (SX126X_IRQ_TX_DONE | SX126X_IRQ_RX_TX_TIMEOUT);
    (void)sx1262_clear_irq_status(0xFFFF);
    (void)sx1262_set_dio_irq_params(irq, irq, 0, 0);

    (void)sx1262_set_tx(3000);
}

static void RadioSleepImpl(void)
{
    s_state = RF_IDLE;
    (void)sx1262_sleep_warm();
}

static void RadioStandbyImpl(void)
{
    s_state = RF_IDLE;
    (void)sx1262_set_standby(SX126X_STANDBY_RC);
}

static void RadioRxImpl(uint32_t timeout)
{
    if (s_modem != MODEM_LORA) return;

    s_state = RF_RX_RUNNING;
    ESP_LOGI(TAG, "RadioRx: timeout=%lu freq=%lu sf=%u bw=%u cont=%d",
             (unsigned long)timeout, (unsigned long)s_freq_hz,
             (unsigned)s_sf, (unsigned)s_bw, s_rx_cont ? 1 : 0);

    const uint16_t irq = (SX126X_IRQ_RX_DONE | SX126X_IRQ_CRC_ERROR | SX126X_IRQ_RX_TX_TIMEOUT);
    (void)sx1262_clear_irq_status(0xFFFF);
    (void)sx1262_set_dio_irq_params(irq, irq, 0, 0);
    (void)sx1262_set_stop_rx_timer_on_preamble(false);
    (void)sx1262_set_lora_symb_num_timeout(s_rx_cont ? 0 : (uint8_t)((s_rx_symb_timeout > 255) ? 255 : s_rx_symb_timeout));

    // timeout: 0 => continuous (LoRaMac uses 0 for continuous)
    (void)sx1262_set_rx(timeout);
}

static void RadioStartCadImpl(uint8_t symbols)
{
    (void)symbols;
    // CAD not implemented yet; report no activity.
    if (s_events && s_events->CadDone) s_events->CadDone(false);
}

static void RadioStopTxContinuousWaveImpl(void)
{
    if (s_cw_timer) {
        (void)esp_timer_stop(s_cw_timer);
    }
    s_state = RF_IDLE;
    (void)sx1262_set_standby(SX126X_STANDBY_RC);
}

static void RadioSetTxContinuousWaveImpl(uint32_t freq, int8_t power, uint16_t time)
{
    s_state = RF_TX_RUNNING;
    s_modem = MODEM_LORA;
    s_freq_hz = freq;

    (void)sx1262_set_standby(SX126X_STANDBY_RC);
    (void)sx1262_set_packet_type(SX126X_PACKET_TYPE_LORA);
    (void)sx1262_set_rf_frequency(freq);
    (void)sx1262_set_pa_config(0x04, 0x07, 0x00, 0x01);
    (void)sx1262_set_ocp(SX126X_OCP_160MA);
    (void)sx1262_set_tx_params(power, SX126X_RAMP_3400_US);
    (void)sx1262_set_dio_irq_params(0, 0, 0, 0);
    (void)sx1262_clear_irq_status(0xFFFF);
    (void)sx1262_set_tx_continuous_wave();

    if (time > 0) {
        if (!s_cw_timer) {
            esp_timer_create_args_t args = {
                .callback = RadioCwTimerCb,
                .arg = NULL,
                .name = "radio_cw",
            };
            if (esp_timer_create(&args, &s_cw_timer) != ESP_OK) {
                ESP_LOGW(TAG, "CW timer create failed");
                s_cw_timer = NULL;
            }
        }
        if (s_cw_timer) {
            (void)esp_timer_stop(s_cw_timer);
            (void)esp_timer_start_once(s_cw_timer, (uint64_t)time * 1000000ULL);
        }
    }
}

void RadioStopTxContinuousWave(void)
{
    RadioStopTxContinuousWaveImpl();
}

static int16_t RadioRssiImpl(RadioModems_t modem)
{
    if (modem != MODEM_LORA) return -127;
    int16_t rssi = -127;
    if(!sx1262_get_rssi_inst(&rssi))return -127;
    return rssi;
}

static void RadioWriteImpl(uint16_t addr, uint8_t data)
{
    (void)sx1262_write_register(addr, &data, 1);
}

static uint8_t RadioReadImpl(uint16_t addr)
{
    uint8_t v = 0;
    (void)sx1262_read_register(addr, &v, 1);
    return v;
}

static void RadioWriteBufferImpl(uint16_t addr, uint8_t *buffer, uint8_t size)
{
    (void)sx1262_write_register(addr, buffer, size);
}

static void RadioReadBufferImpl(uint16_t addr, uint8_t *buffer, uint8_t size)
{
    (void)sx1262_read_register(addr, buffer, size);
}

static void RadioSetSyncWordImpl(uint8_t data)
{
    // For SX126x LoRa, syncword is 2 bytes. Keep LSB default (0x44) and allow MSB override.
    uint8_t sw[2] = { data, 0x44 };
    (void)sx1262_write_register(REG_LORA_SYNCWORD_MSB, sw, sizeof(sw));
}

static void RadioSetMaxPayloadLengthImpl(RadioModems_t modem, uint8_t max)
{
    (void)modem;
    s_max_payload = max;
}

static void RadioSetPublicNetworkImpl(bool enable)
{
    uint8_t sw[2];
    if (enable) {
        sw[0] = 0x34; sw[1] = 0x44;
    } else {
        sw[0] = 0x14; sw[1] = 0x24;
    }
    (void)sx1262_write_register(REG_LORA_SYNCWORD_MSB, sw, sizeof(sw));
}

static uint32_t RadioGetWakeupTimeImpl(void)
{
    return 0;
}

static void RadioIrqProcessImpl(void)
{
    uint16_t irq = 0;
    if (!sx1262_get_irq_status(&irq)) return;
    if (irq == 0 || irq == 0xFFFF) return;
    (void)sx1262_clear_irq_status(irq);
    ESP_LOGI(TAG, "RadioIrqProcess: irq=0x%04X prev_state=%d", (unsigned)irq, (int)s_state);

    if (irq & SX126X_IRQ_TX_DONE) {
        s_state = RF_IDLE;
        if (s_events && s_events->TxDone) s_events->TxDone();
        return;
    }

    if (irq & SX126X_IRQ_RX_DONE) {
        uint8_t len = 0, start = 0;
        (void)sx1262_get_rx_buffer_status(&len, &start);
        // len is uint8_t and buffer is 255 bytes; it cannot exceed the buffer size.
        (void)sx1262_read_buffer(start, s_rx_buf, len);

        int16_t rssi = -127;
        int8_t snr = 0;
        (void)sx1262_get_packet_status(&rssi, &snr);

        s_state = RF_IDLE;
        if (s_events && s_events->RxDone) s_events->RxDone(s_rx_buf, len, rssi, snr);
        return;
    }

    if (irq & SX126X_IRQ_CRC_ERROR) {
        s_state = RF_IDLE;
        if (s_events && s_events->RxError) s_events->RxError();
        return;
    }

    if (irq & SX126X_IRQ_RX_TX_TIMEOUT) {
        RadioState_t prev = s_state;
        s_state = RF_IDLE;
        if (prev == RF_TX_RUNNING) {
            if (s_events && s_events->TxTimeout) s_events->TxTimeout();
        } else {
            if (s_events && s_events->RxTimeout) s_events->RxTimeout();
        }
        return;
    }
}

static void RadioRxBoostedImpl(uint32_t timeout)
{
    RadioRxImpl(timeout);
}

static void RadioSetRxDutyCycleImpl(uint32_t rxTime, uint32_t sleepTime)
{
    (void)rxTime;
    (void)sleepTime;
    // Not implemented
}

const struct Radio_s Radio = {
    .Init = RadioInitImpl,
    .GetStatus = RadioGetStatusImpl,
    .SetModem = RadioSetModemImpl,
    .SetChannel = RadioSetChannelImpl,
    .IsChannelFree = RadioIsChannelFreeImpl,
    .Random = RadioRandomImpl,
    .SetRxConfig = RadioSetRxConfigImpl,
    .SetTxConfig = RadioSetTxConfigImpl,
    .CheckRfFrequency = RadioCheckRfFrequencyImpl,
    .TimeOnAir = RadioTimeOnAirImpl,
    .Send = RadioSendImpl,
    .Sleep = RadioSleepImpl,
    .Standby = RadioStandbyImpl,
    .Rx = RadioRxImpl,
    .StartCad = RadioStartCadImpl,
    .SetTxContinuousWave = RadioSetTxContinuousWaveImpl,
    .Rssi = RadioRssiImpl,
    .Write = RadioWriteImpl,
    .Read = RadioReadImpl,
    .WriteBuffer = RadioWriteBufferImpl,
    .ReadBuffer = RadioReadBufferImpl,
    .SetSyncWord = RadioSetSyncWordImpl,
    .SetMaxPayloadLength = RadioSetMaxPayloadLengthImpl,
    .SetPublicNetwork = RadioSetPublicNetworkImpl,
    .GetWakeupTime = RadioGetWakeupTimeImpl,
    .IrqProcess = RadioIrqProcessImpl,
    .RxBoosted = RadioRxBoostedImpl,
    .SetRxDutyCycle = RadioSetRxDutyCycleImpl,
};
