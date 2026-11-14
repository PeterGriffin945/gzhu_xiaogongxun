/**
 * @file wifi_sta.c
 * @brief Wi-Fi STA 连接管理
 */
#include "wifi_sta.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "app_config.h"

static const char *TAG = "wifi";

static volatile bool s_connected;
static char s_ip[16] = "0.0.0.0";

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;

    switch (event_id) {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "connecting to \"%s\" ...", WIFI_SSID);
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *ev =
            (const wifi_event_sta_disconnected_t *)event_data;
        s_connected = false;
        strcpy(s_ip, "0.0.0.0");
        ESP_LOGW(TAG, "disconnected (reason=%d), supervisor will retry",
                 ev ? ev->reason : -1);
        break;
    }
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    (void)event_id;

    const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)event_data;
    esp_ip4addr_ntoa(&ev->ip_info.ip, s_ip, sizeof(s_ip));
    s_connected = true;
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "connected! open http://%s/", s_ip);
}

static void wifi_supervisor_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!s_connected) {
            esp_wifi_connect();
        }
    }
}

esp_err_t wifi_sta_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (netif == NULL) {
        return ESP_FAIL;
    }
    esp_netif_set_hostname(netif, "mecanum-car");

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &ip_event_handler, NULL, NULL);

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, WIFI_PASS,
            sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;   /* 兼容 WPA/WPA2/WPA3 */
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);

    BaseType_t ret = xTaskCreate(wifi_supervisor_task, "wifi_sup", 3072, NULL, 3, NULL);
    if (ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "STA started, PS disabled (WIFI_PS_NONE)");
    return ESP_OK;
}

bool wifi_sta_connected(void)
{
    return s_connected;
}

const char *wifi_sta_ip(void)
{
    return s_ip;
}
