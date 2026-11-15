/**
 * @file wifi_sta.c
 * @brief Wi-Fi STA 连接管理 + AP 兜底（路由器不可用时也能连车调试）
 *
 * APSTA 模式：始终开启一个调试热点，STA 同时连接路由器。
 * 这样即使现场路由器改名/换频段/密码错误，手机仍可连 "mecanum-car"
 * 打开 http://192.168.4.1/ 调试。
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
static volatile uint8_t s_fail_count;
static volatile bool s_scanned;
static char s_ip[16] = "0.0.0.0";

static const char *reason_name(uint8_t reason)
{
    switch (reason) {
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE";
    case 3:   return "AUTH_FAIL";
    case 4:   return "ASSOC_EXPIRE";
    case 5:   return "ASSOC_TOOMANY";
    case 8:   return "ASSOC_LEAVE";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL(202)";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default:  return "OTHER";
    }
}

static void log_scan(void)
{
    /* 静态缓冲，避免扫描时占用 supervisor 任务栈 */
    static wifi_ap_record_t records[8];
    wifi_scan_config_t scan = { .show_hidden = true };
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return;
    }
    uint16_t count = 0;
    if (esp_wifi_scan_get_ap_num(&count) != ESP_OK || count == 0) {
        ESP_LOGW(TAG, "scan: no AP found (check 2.4GHz band / antenna)");
        return;
    }
    if (count > 8) {
        count = 8;
    }
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        return;
    }
    ESP_LOGI(TAG, "scan found %u AP(s):", (unsigned)count);
    for (uint16_t i = 0; i < count; ++i) {
        ESP_LOGI(TAG, "  \"%s\" rssi=%d ch=%u auth=%d",
                 (const char *)records[i].ssid, records[i].rssi,
                 records[i].primary, (int)records[i].authmode);
    }
}

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
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "associated, waiting for DHCP");
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *ev =
            (const wifi_event_sta_disconnected_t *)event_data;
        uint8_t reason = ev ? ev->reason : 0xFF;
        s_connected = false;
        strcpy(s_ip, "0.0.0.0");
        if (s_fail_count < 255) {
            s_fail_count++;
        }
        ESP_LOGW(TAG, "disconnected: %s (%u), attempt %u",
                 reason_name(reason), (unsigned)reason, (unsigned)s_fail_count);
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
    s_fail_count = 0;
    s_scanned = false;
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "connected! open http://%s/", s_ip);
}

static void wifi_supervisor_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (s_connected) {
            continue;
        }
        /* 连续失败后做一次扫描，帮助判断是 SSID 写错、5GHz-only 还是信号太弱 */
        if (s_fail_count >= 5 && !s_scanned) {
            s_scanned = true;
            log_scan();
        }
        if (s_fail_count > 0 && (s_fail_count % 10) == 0) {
            ESP_LOGW(TAG, "still disconnected (%u attempts), retrying",
                     (unsigned)s_fail_count);
        }
        esp_wifi_connect();
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

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        return ESP_FAIL;
    }
    esp_netif_set_hostname(sta_netif, "mecanum-car");

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGW(TAG, "AP netif unavailable, fallback AP disabled");
    }

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

    wifi_config_t sta_cfg = { 0 };
    strncpy((char *)sta_cfg.sta.ssid, WIFI_SSID, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, WIFI_PASS,
            sizeof(sta_cfg.sta.password) - 1);
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;   /* 兼容 WPA/WPA2/WPA3 */
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    wifi_config_t ap_cfg = { 0 };
    strncpy((char *)ap_cfg.ap.ssid, WIFI_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
    strncpy((char *)ap_cfg.ap.password, WIFI_AP_PASS,
            sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(WIFI_AP_SSID);
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 2;

    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (err != ESP_OK) {
        return err;
    }
    if (ap_netif != NULL) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "AP config failed: %s", esp_err_to_name(err));
        }
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);

    BaseType_t ret = xTaskCreate(wifi_supervisor_task, "wifi_sup", 4096, NULL, 3, NULL);
    if (ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    uint8_t mac[6] = { 0 };
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "STA \"%s\" retry enabled, MAC %02X:%02X:%02X:%02X:%02X:%02X",
             WIFI_SSID, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (ap_netif != NULL) {
        ESP_LOGI(TAG, "fallback AP \"%s\" / \"%s\" -> http://192.168.4.1/",
                 WIFI_AP_SSID, WIFI_AP_PASS);
    }
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
