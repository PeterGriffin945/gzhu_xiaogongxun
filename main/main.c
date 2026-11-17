/**
 * @file main.c
 * @brief 工创赛麦轮小车 —— ESP32-S3 高实时性底层驱动 + Web 调试系统
 *
 * 任务/核心分配：
 *   Core 0 : Wi-Fi / HTTP 服务 / 1kHz 步进速度斜坡
 *   Core 1 : 20kHz 脉冲由 GPTimer ISR 产生；ADS1115 采样任务、100Hz 底盘控制
 */
#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "ads1115.h"
#include "app_config.h"
#include "chassis.h"
#include "competition.h"
#include "safety.h"
#include "i2c_bus.h"
#include "k230_link.h"
#include "line_follow.h"
#include "lu9685.h"
#include "settings.h"
#include "stepper.h"
#include "web_server.h"
#include "wifi_sta.h"

static const char *TAG = "app";

static void print_banner(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " Mecanum Car Controller  |  ESP32-S3  |  ESP-IDF");
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, " M1 RR  STEP=%d DIR=%d", PIN_M1_RR_STEP, PIN_M1_RR_DIR);
    ESP_LOGI(TAG, " M2 RF  STEP=%d DIR=%d", PIN_M2_RF_STEP, PIN_M2_RF_DIR);
    ESP_LOGI(TAG, " M3 LF  STEP=%d DIR=%d (inverted)", PIN_M3_LF_STEP, PIN_M3_LF_DIR);
    ESP_LOGI(TAG, " M4 LR  STEP=%d DIR=%d (inverted)", PIN_M4_LR_STEP, PIN_M4_LR_DIR);
    ESP_LOGI(TAG, " M5 LIFT STEP=%d DIR=%d", PIN_M5_LIFT_STEP, PIN_M5_LIFT_DIR);
    ESP_LOGI(TAG, " EN: chassis=GPIO%d lift=GPIO%d (high=release)",
             PIN_EN_CHASSIS, PIN_EN_LIFT);
    ESP_LOGI(TAG, " I2C: SDA=%d SCL=%d @%dHz  TCA9548A=0x%02X RST=GPIO%d",
             PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ, TCA9548A_ADDR, PIN_TCA_RESET);
    ESP_LOGI(TAG, "==================================================");
}

void app_main(void)
{
    print_banner();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    /* 最先初始化步进：EN 立即拉高释放，避免上电电机锁死发热 */
    ESP_ERROR_CHECK(stepper_init());

    /* I2C 总线 + TCA9548A */
    ESP_ERROR_CHECK(i2c_bus_init());

    /* 双 ADS1115 采样任务（Core 1） */
    ESP_ERROR_CHECK(ads1115_init());
    ESP_ERROR_CHECK(ads1115_start());

    /* LU9685 舵机板（失败不阻塞系统） */
    if (lu9685_init() != ESP_OK) {
        ESP_LOGW(TAG, "servo board offline, chassis functions still available");
    }

    line_follow_init();
    safety_init();
    ESP_ERROR_CHECK(chassis_init());

    /* 从 NVS 恢复上次保存的 PID/阈值/速度/舵机校准 */
    if (settings_init() != ESP_OK) {
        ESP_LOGW(TAG, "settings storage unavailable, using defaults");
    }

    /* K230 链路失败不影响当前半自动流程：人工对位 + 执行取放，视觉板可断开。 */
    if (k230_link_init() != ESP_OK) {
        ESP_LOGW(TAG, "K230 UART unavailable, manual-positioning flow still available");
    }
    ESP_ERROR_CHECK(competition_init());

    /* 恢复上次保存的机械臂/舵机标定（角度、Z 步数、开合、速度角等） */
    competition_config_t saved_cfg;
    bool saved_confirmed = false;
    if (settings_load_competition(&saved_cfg, &saved_confirmed) == ESP_OK) {
        if (competition_set_config(&saved_cfg, saved_confirmed) != ESP_OK) {
            ESP_LOGW(TAG, "saved arm config rejected, using defaults");
        }
    }

    /* Wi-Fi 与 Web 控制台：Wi-Fi 初始化失败不要重启，保留串口日志便于定位 */
    if (wifi_sta_init() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed; serial console remains available");
    }
    ESP_ERROR_CHECK(web_server_start());

    ESP_LOGI(TAG, "system ready, connect to Wi-Fi \"%s\" and open the IP above",
             WIFI_SSID);

    /* 主循环只做低频健康检查，所有实时工作都在独立任务/ISR 中 */
    uint32_t last_seq = 0;
    int stale = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        uint32_t seq = ads1115_get_seq();
        if (seq == last_seq) {
            stale++;
            if (stale >= 3) {
                ESP_LOGW(TAG, "sensor sampling stalled (seq=%" PRIu32 ")", seq);
                stale = 0;
            }
        } else {
            stale = 0;
        }
        last_seq = seq;
    }
}
