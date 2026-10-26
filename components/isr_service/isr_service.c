#include "isr_service.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "isr_service";
static bool s_installed = false;

bool isr_service_ensure_installed(void)
{
    if (s_installed) return true;

    esp_err_t e = gpio_install_isr_service(0);
    if (e == ESP_OK || e == ESP_ERR_INVALID_STATE) {
        s_installed = true;
        return true;
    }

    ESP_LOGW(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(e));
    return false;
}
