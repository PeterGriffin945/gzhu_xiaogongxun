/**
 * @file ads1115.c
 * @brief 双 ADS1115 乒乓交替采样
 *
 * 流水线（两块 ADS 并行转换，互不等待）：
 *   1. 触发 A 板 CHn，随后触发 B 板 CHn；
 *   2. 等 A 板转换完成 → 读 A 板结果 → 立刻触发 A 板 CHn+1；
 *   3. 等 B 板转换完成 → 读 B 板结果 → 立刻触发 B 板 CHn+1；
 *   4. 重复 4 轮，8 路全部刷新 ≈ 6~8ms（< 12ms 目标）。
 *
 * 860SPS 单次转换 ≈ 1.163ms，等待 1.25ms 留足余量。
 */
#include "ads1115.h"

#include <math.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "app_config.h"
#include "i2c_bus.h"

static const char *TAG = "ads1115";

#define ADS_REG_CONV    0x00
#define ADS_REG_CONFIG  0x01

#define ADS_CFG_OS         0x8000
#define ADS_CFG_MUX_BASE   0x4000        /* 100b = AIN0-GND，+ch<<12 */
#define ADS_CFG_PGA_6V144  0x0000        /* ±6.144V，PGA=2/3 */
#define ADS_CFG_MODE_SINGLE 0x0100
#define ADS_CFG_DR_860SPS  0x00E0
#define ADS_CFG_COMP_OFF   0x0003

#define ADS_FULL_SCALE_V   6.144f
#define ADS_COUNT          2

static i2c_master_dev_handle_t s_dev[ADS_COUNT];
static bool s_present[ADS_COUNT];

static SemaphoreHandle_t s_data_mutex;
static float s_voltage[LINE_SENSOR_COUNT];
static uint32_t s_seq;
static volatile bool s_ready;

static const uint8_t s_tca_ch[ADS_COUNT] = { TCA_CH_ADS_5678, TCA_CH_ADS_1234 };
static const uint16_t s_addr[ADS_COUNT] = { ADS_ADDR_PROBE5678, ADS_ADDR_PROBE1234 };

/* ---------------- ADS1115 原语 ---------------- */

static esp_err_t ads_start_conversion(i2c_master_dev_handle_t dev, int ch)
{
    uint16_t cfg = ADS_CFG_OS |
                   (uint16_t)((0x04 + ch) << 12) |
                   ADS_CFG_MODE_SINGLE |
                   ADS_CFG_DR_860SPS |
                   ADS_CFG_COMP_OFF;
    uint8_t buf[3] = { ADS_REG_CONFIG, (uint8_t)(cfg >> 8), (uint8_t)(cfg & 0xFF) };
    return i2c_master_transmit(dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t ads_read_conversion(i2c_master_dev_handle_t dev, int16_t *raw)
{
    uint8_t reg = ADS_REG_CONV;
    uint8_t buf[2] = { 0 };
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, buf, sizeof(buf),
                                                I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        *raw = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    }
    return err;
}

/* ---------------- 精确等待 ---------------- */

static void wait_until_us(int64_t deadline_us)
{
    while (1) {
        int64_t now = esp_timer_get_time();
        int64_t remain = deadline_us - now;
        if (remain <= 0) {
            return;
        }
        if (remain > 800) {
            vTaskDelay(pdMS_TO_TICKS(1));
        } else if (remain > 0) {
            /* 最后不足 800us 精确自旋；ADS 任务优先级低于底盘控制，
               自旋期间底盘 100Hz 任务可随时抢占，不影响实时性 */
            esp_rom_delay_us((uint32_t)(remain > 400 ? 400 : remain));
        }
    }
}

/* ---------------- 采样任务 ---------------- */
static void ads_sample_task(void *arg)
{
    (void)arg;

    int64_t trigger_us[ADS_COUNT] = { 0 };
    int16_t raw[ADS_COUNT][4] = { { 0 } };
    int64_t last_err_log = 0;
    TickType_t last_wake = xTaskGetTickCount();

    /* 首次触发所有在线设备的 CH0 */
    for (int d = 0; d < ADS_COUNT; ++d) {
        if (!s_present[d]) {
            continue;
        }
        if (!i2c_bus_lock(I2C_TIMEOUT_MS)) {
            continue;
        }
        esp_err_t err = tca_select(s_tca_ch[d]);
        if (err == ESP_OK) {
            err = ads_start_conversion(s_dev[d], 0);
        }
        i2c_bus_unlock();
        trigger_us[d] = esp_timer_get_time();
        if (err != ESP_OK) {
            s_present[d] = false;
            ESP_LOGW(TAG, "ADS%d start failed: %s", d, esp_err_to_name(err));
        }
    }

    while (1) {
        for (int ch = 0; ch < 4; ++ch) {
            for (int d = 0; d < ADS_COUNT; ++d) {
                if (!s_present[d]) {
                    continue;
                }

                wait_until_us(trigger_us[d] + ADS_CONV_TIME_US);

                if (!i2c_bus_lock(I2C_TIMEOUT_MS)) {
                    trigger_us[d] = esp_timer_get_time();
                    continue;
                }
                esp_err_t err = tca_select(s_tca_ch[d]);
                if (err == ESP_OK) {
                    err = ads_read_conversion(s_dev[d], &raw[d][ch]);
                }
                /* 读完后立刻触发下一路；ch3 之后回卷触发 ch0，
                   保证下一轮 ch0 读到的不是上一轮 ch3 的旧值 */
                if (err == ESP_OK) {
                    err = ads_start_conversion(s_dev[d], (ch + 1) & 3);
                }
                i2c_bus_unlock();

                if (err == ESP_OK) {
                    trigger_us[d] = esp_timer_get_time();
                } else {
                    int64_t now = esp_timer_get_time();
                    if (now - last_err_log > 5000000) {
                        last_err_log = now;
                        ESP_LOGW(TAG, "ADS%d ch%d error: %s", d, ch,
                                 esp_err_to_name(err));
                    }
                    trigger_us[d] = esp_timer_get_time();
                }
            }
        }

        /* 单次总线错误时沿用上一拍缓存值，避免整帧停止刷新 */
        if (s_present[0] && s_present[1]) {
            float local[LINE_SENSOR_COUNT];
            const float scale = ADS_FULL_SCALE_V / 32768.0f;

            /* 0x48 → 探头 5..8 (index 4..7)；0x49 → 探头 1..4 (index 0..3) */
            for (int ch = 0; ch < 4; ++ch) {
                local[4 + ch] = (float)raw[0][ch] * scale;
                local[ch] = (float)raw[1][ch] * scale;
            }

            if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                for (int i = 0; i < LINE_SENSOR_COUNT; ++i) {
                    s_voltage[i] = local[i];
                }
                s_seq++;
                s_ready = true;
                xSemaphoreGive(s_data_mutex);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ADS_REFRESH_MS));
    }
}

/* ---------------- 对外接口 ---------------- */
esp_err_t ads1115_init(void)
{
    if (i2c_bus_handle() == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_data_mutex = xSemaphoreCreateMutex();
    if (s_data_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int d = 0; d < ADS_COUNT; ++d) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = s_addr[d],
            .scl_speed_hz = I2C_FREQ_HZ,
        };
        esp_err_t err = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev[d]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "add ADS1115 @0x%02X failed: %s", s_addr[d],
                     esp_err_to_name(err));
            return err;
        }

        bool found = false;
        if (i2c_bus_lock(I2C_TIMEOUT_MS)) {
            if (tca_select(s_tca_ch[d]) == ESP_OK) {
                found = (i2c_master_probe(i2c_bus_handle(), s_addr[d],
                                          I2C_TIMEOUT_MS) == ESP_OK);
            }
            i2c_bus_unlock();
        }
        s_present[d] = found;
        if (found) {
            ESP_LOGI(TAG, "ADS1115 @0x%02X on TCA CH%d OK", s_addr[d], s_tca_ch[d]);
        } else {
            ESP_LOGW(TAG, "ADS1115 @0x%02X on TCA CH%d not found", s_addr[d],
                     s_tca_ch[d]);
        }
    }
    return ESP_OK;
}

esp_err_t ads1115_start(void)
{
    /* 优先级略低于 100Hz 底盘控制，采样等待可被控制环随时抢占 */
    BaseType_t ret = xTaskCreatePinnedToCore(ads_sample_task, "ads_sample",
                                             4096, NULL, 4, NULL, 1);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool ads1115_get_latest(float v[8], uint32_t *seq)
{
    if (s_data_mutex == NULL || v == NULL) {
        return false;
    }
    bool ok = false;
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (int i = 0; i < LINE_SENSOR_COUNT; ++i) {
            v[i] = s_voltage[i];
        }
        if (seq != NULL) {
            *seq = s_seq;
        }
        ok = s_ready;
        xSemaphoreGive(s_data_mutex);
    }
    return ok;
}

uint32_t ads1115_get_seq(void)
{
    uint32_t seq = 0;
    if (s_data_mutex != NULL &&
        xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        seq = s_seq;
        xSemaphoreGive(s_data_mutex);
    }
    return seq;
}

bool ads1115_is_ready(void)
{
    return s_ready;
}

float ads1115_get_voltage(int index)
{
    float v = 0.0f;
    if (index < 0 || index >= LINE_SENSOR_COUNT || s_data_mutex == NULL) {
        return 0.0f;
    }
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        v = s_voltage[index];
        xSemaphoreGive(s_data_mutex);
    }
    return v;
}
