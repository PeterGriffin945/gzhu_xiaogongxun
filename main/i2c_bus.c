/**
 * @file i2c_bus.c
 * @brief I2C 主总线与 TCA9548A 多路复用器
 */
#include "i2c_bus.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "app_config.h"

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_mux;
static SemaphoreHandle_t s_mutex;
static bool s_tca_ok;

static void tca_hardware_reset(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_TCA_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    gpio_set_level(PIN_TCA_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));      /* 低电平保持 20ms */
    gpio_set_level(PIN_TCA_RESET, 1);
    esp_rom_delay_us(500);
}

esp_err_t i2c_bus_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* 先硬复位 TCA9548A，再建立总线 */
    tca_hardware_reset();

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,                    /* 自动选择 I2C 控制器 */
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t mux_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9548A_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &mux_cfg, &s_mux);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add TCA9548A failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 探测 + 关闭所有通道 */
    s_tca_ok = (i2c_master_probe(s_bus, TCA9548A_ADDR, I2C_TIMEOUT_MS) == ESP_OK);
    if (!s_tca_ok) {
        ESP_LOGW(TAG, "TCA9548A @0x%02X not found", TCA9548A_ADDR);
    } else {
        tca_deselect();
        ESP_LOGI(TAG, "TCA9548A ready @0x%02X (SDA=%d SCL=%d %dHz)",
                 TCA9548A_ADDR, PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
    }
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_handle(void)
{
    return s_bus;
}

esp_err_t tca_select(uint8_t channel)
{
    if (channel > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mux == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t mask = (uint8_t)(1u << channel);
    return i2c_master_transmit(s_mux, &mask, 1, I2C_TIMEOUT_MS);
}

esp_err_t tca_deselect(void)
{
    if (s_mux == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t mask = 0x00;
    return i2c_master_transmit(s_mux, &mask, 1, I2C_TIMEOUT_MS);
}

bool tca_available(void)
{
    return s_tca_ok;
}

bool i2c_bus_lock(uint32_t timeout_ms)
{
    if (s_mutex == NULL) {
        return false;
    }
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY
                                                  : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(s_mutex, ticks) == pdTRUE;
}

void i2c_bus_unlock(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}
