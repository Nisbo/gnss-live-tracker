#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lorawan_port.h"
#include "tracker_credentials.h"
#include "tracker_backup.h"
#include "gnss_nmea.h"
#include "tracker_display.h"
#include "tracker_web.h"
#include "radio_settings.h"
#include "radio_policy.h"
#include "tracker_log.h"
#include "gnss_diagnostics.h"
#include "satellite_log.h"
#include "import_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include <string.h>
#if CONFIG_NVS_ENCRYPTION || CONFIG_SECURE_FLASH_ENC_ENABLED || CONFIG_SECURE_BOOT
#error Irreversible security settings are forbidden for this test
#endif
static const char *TAG="v2-test";
static tracker_gnss_t gnss;
static bool gnss_uart_ready;
static int64_t last_uplink_completed_ms;
static void start_gnss(void) {
    tracker_gnss_init(&gnss);
    /* V2 factory example: VGNSS_CTRL = Vext = 3, HIGH enables GNSS. */
    gpio_config_t power={.pin_bit_mask=1ULL<<3,.mode=GPIO_MODE_OUTPUT};
    esp_err_t err=gpio_config(&power);
    if(err==ESP_OK) err=gpio_set_level(GPIO_NUM_3,1);
    uart_config_t cfg={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    if(err==ESP_OK) err=uart_param_config(UART_NUM_1,&cfg);
    if(err==ESP_OK) err=uart_set_pin(UART_NUM_1,34,33,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(err==ESP_OK) err=uart_driver_install(UART_NUM_1,8192,0,0,NULL,0);
    gnss_uart_ready=err==ESP_OK;
    ESP_LOGI(TAG,"GNSS UART ready=%d, 115200 baud RX33/TX34, power GPIO3 HIGH (%s)",gnss_uart_ready,esp_err_to_name(err));
}
static void poll_gnss(int64_t now_ms) {
    static char line[256];static size_t length;
    static unsigned configuration_queries;static int64_t next_configuration_query;
    if(!gnss_uart_ready)return;
    static bool gst_requested;
    if(!gst_requested&&now_ms>=5000){const char *request="$CFGMSG,0,7,1\r\n$CFGNAV\r\n$CFGMSG,0,7\r\n";uart_write_bytes(UART_NUM_1,request,strlen(request));gst_requested=true;}
    if(gnss_diagnostics_take_probe()){const char *query="$CFGSYS\r\n$CFGNAV\r\n$CFGMSG,0,7\r\n";uart_write_bytes(UART_NUM_1,query,strlen(query));}
    if(!gnss.configuration_known&&configuration_queries<3&&now_ms>=next_configuration_query){const char *query="$CFGSYS\r\n";uart_write_bytes(UART_NUM_1,query,strlen(query));configuration_queries++;next_configuration_query=now_ms+10000;}
    uint8_t bytes[512];int n=uart_read_bytes(UART_NUM_1,bytes,sizeof(bytes),0);
    for(int i=0;i<n;i++) {
        char c=(char)bytes[i];
        if(c=='$') {length=0;line[length++]=c;}
        else if(c=='\n'&&length) {
            line[length]=0;
            gnss_diagnostics_line(line,now_ms);
            if(tracker_nmea_line(&gnss,line,now_ms)&&gnss.sentences<=12) ESP_LOGI(TAG,"GNSS NMEA: %s",line);
            length=0;
        } else if(c!='\r'&&length) {
            if(length<sizeof(line)-1)line[length++]=c;else length=0;
        }
    }
}
static void joined(bool ok) { if(ok)tracker_radio_joined(); ESP_LOGI(TAG,"OTAA JOIN %s",ok ? "ACCEPTED" : "FAILED"); }
static void sent(bool ok,uint8_t dr,int16_t rssi) { last_uplink_completed_ms=esp_timer_get_time()/1000;tracker_web_message_complete(ok,last_uplink_completed_ms); ESP_LOGI(TAG,"Uplink completion=%d DR=%u (unconfirmed, check TTN)",ok,dr); }
void app_main(void) {
    ESP_LOGI(TAG,"Heltec Tracker V2: region-configured / Class A / OTAA; no eFuse writes");
    ESP_ERROR_CHECK(nvs_flash_init());
    tracker_backup_init();
    tracker_radio_init();
    tracker_credentials_init();
    ESP_LOGI(TAG,"Autonomous track logger ready=%d",tracker_log_init());
    gnss_diagnostics_init();
    satellite_log_init();
    if(!tracker_backup_locked())import_log_init();
    start_gnss();
    tracker_display_init();
    ESP_LOGI(TAG,"V2 display initialized: 160x80, Galileo highlighted, all systems, 1 Hz refresh");
    tracker_radio_config_t radio=tracker_radio_config();
    lorawan_cfg_t cfg={.region=lorawan_port_region(radio.region),.as923_group=tracker_region_as923(radio.region)?radio.region-4:1,.uplink_dwell=radio.dwell,.channel_block=radio.channel_block,.device_class=CLASS_A,
      .session_store=tracker_credentials_ready()&&tracker_radio_ready()?LORAWAN_SESSION_STORE_NVS:LORAWAN_SESSION_STORE_NONE,.session_save_every=1,.adr_enabled=radio.adr,
      /* EU868 joins use DR0 in RX2; the JoinAccept supplies session RX2 settings. */
      .join_datarate=radio.region==0?0:-1,.rx2_frequency=radio.region==0?869525000:0,.rx2_datarate=radio.region==0?0:-1,.tx_datarate=tracker_region_dr(radio.region,radio.sf,radio.wideband)};
    lorawan_port_set_join_callback(joined);
    lorawan_port_set_uplink_callback(sent);
    if(!lorawan_port_init(&cfg)) { ESP_LOGE(TAG,"Radio/MAC initialization failed"); return; }
    MibRequestConfirm_t rx_error={.Type=MIB_SYSTEM_MAX_RX_ERROR};
    rx_error.Param.SystemMaxRxError=150;
    if(LoRaMacMibSetRequestConfirm(&rx_error)!=LORAMAC_STATUS_OK) {
        ESP_LOGE(TAG,"Cannot configure RX timing tolerance"); return;
    }
    ESP_LOGI(TAG,"Local web interface ready=%d",tracker_web_start());
    int64_t next_join=0, next_position=0, next_log=0;
    int64_t last_position_us=-1,next_control_us=0;
    uint16_t position_interval=radio.position_interval_s;
    uint16_t sequence=0;
    bool first_fix_reported=false;
    bool startup_status_reported=false;
    int64_t next_display=0,last_queued_ms=0;uint8_t last_queued_port=0;
    while(1) {
        lorawan_port_process();
        int64_t now=esp_timer_get_time();
        tracker_backup_poll();
        poll_gnss(now/1000);
        if(!tracker_backup_locked()){tracker_log_poll(&gnss,now/1000);satellite_log_poll(&gnss,now/1000);}
        tracker_web_poll();
        tracker_display_poll(now/1000);
        tracker_radio_poll();
        tracker_credentials_poll();
        bool credentials_ready=tracker_credentials_ready()&&tracker_radio_ready()&&!tracker_backup_locked();
        radio=tracker_radio_config();
        if(position_interval!=radio.position_interval_s){position_interval=radio.position_interval_s;next_position=tracker_radio_position_due(last_position_us,position_interval,next_position);}
        if(!startup_status_reported&&now>=15000000LL) {
            startup_status_reported=true;next_position=now;
        }
        bool fix=tracker_gnss_has_fix(&gnss,now/1000);
        /* Report first fix promptly, independent of the configured regular interval. */
        if(fix&&!first_fix_reported&&sequence>0)next_position=now;
        if(now>=next_log) {
            ESP_LOGI(TAG,"GNSS status: NMEA=%lu bad_checksum=%lu fix=%d quality=%u used=%u",
                     (unsigned long)gnss.sentences,(unsigned long)gnss.checksum_errors,fix,gnss.quality,gnss.used);
            next_log=now+10000000LL;
        }
        if(credentials_ready && !lorawan_port_is_joined() && !lorawan_port_is_join_pending() && now>=next_join) {
            ESP_LOGI(TAG,"Request OTAA join");
            tracker_credentials_t credentials=tracker_credentials_copy();
            lorawan_port_join_otaa(credentials.dev_eui,credentials.join_eui,credentials.app_key,3);
            memset(&credentials,0,sizeof(credentials));
            next_join=now+300000000LL;
        }
        int8_t next_dr=0;unsigned budget=LoRaMacTxBudget(&next_dr),packet_bytes=tracker_packet_size(tracker_radio_packet_bytes(radio.profile),budget);
        if(credentials_ready&&lorawan_port_is_joined()&&now>=next_position&&!packet_bytes&&now>=next_control_us&&lorawan_port_can_tx(0)){
            if(lorawan_port_send_unconfirmed(0,NULL,0)){last_queued_ms=now/1000;last_queued_port=0;tracker_web_message_queued(0,NULL,0,last_queued_ms);next_control_us=now+(int64_t)position_interval*1000000LL;}
        }
        if(credentials_ready && lorawan_port_is_joined() && now>=next_position && packet_bytes && lorawan_port_can_tx(packet_bytes)) {
            uint8_t payload[38];
            size_t length=packet_bytes<=11?tracker_position_small(&gnss,now/1000,sequence,packet_bytes,payload):tracker_position_packet(&gnss,now/1000,sequence,radio.profile,payload);
            if(lorawan_port_send_unconfirmed(10,payload,length)) {
                last_queued_ms=now/1000;last_queued_port=10;tracker_web_message_queued(10,payload,length,last_queued_ms);
                ESP_LOGI(TAG,"GNSS queued: FPort 10 sequence=%u fix=%d bytes=%u",sequence,fix,(unsigned)length);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG,payload,length,ESP_LOG_INFO);
                if(fix)first_fix_reported=true;
                sequence++;
                last_position_us=now;next_position=tracker_radio_position_due(last_position_us,position_interval,next_position);
            }
        }
        bool display_refresh=tracker_display_refresh_requested();
        if(now>=next_display||display_refresh) {
            int64_t due=next_position;uint8_t due_port=10;
            tracker_web_lora_timing(last_queued_ms,last_uplink_completed_ms,last_queued_port,due/1000,due_port,next_join/1000);
            tracker_display_lora_timing(last_uplink_completed_ms,due/1000,due_port);
            tracker_web_publish(&gnss,now/1000,lorawan_port_is_joined());
            tracker_display_update(&gnss,now/1000,lorawan_port_is_joined());
            next_display=now+1000000LL;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
