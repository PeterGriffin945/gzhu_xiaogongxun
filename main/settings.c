/**
 * @file settings.c
 * @brief 调参参数 NVS 持久化
 */
#include "settings.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "app_config.h"
#include "chassis.h"
#include "line_follow.h"
#include "lu9685.h"
#include "stepper.h"

#define SETTINGS_NAMESPACE "tuning"
#define SETTINGS_KEY       "blob"
#define SETTINGS_COMP_KEY  "comp"
#define SETTINGS_MAGIC     0x54554E45U   /* 'TUNE' */
#define COMP_SETTINGS_MAGIC 0x434F4D50U  /* 'COMP' */
/* v2：视觉目标点从 QVGA(160,120) 改为 VGA(320,240)，旧标定作废避免错位。 */
#define COMP_SETTINGS_VERSION 2
/* v2：速度/加速度单位从整步改为 1/16 微步，旧值必须丢弃后按新默认重来 */
#define SETTINGS_VERSION   2
#define SETTINGS_DEBOUNCE_US (2 * 1000 * 1000)

static const char *TAG = "settings";

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    float kp;
    float ki;
    float kd;
    float threshold;
    float max_omega;
    float accel;
    float chassis_sps;
    float lift_sps;
    uint8_t black_high;
    uint8_t reverse;
    uint8_t proto;
    uint8_t center[3];      /* CH0 / CH4 / CH8 */
    uint8_t reserved[9];
} settings_blob_t;

/* 机械臂/舵机标定：所有动作步骤共用同一份配置，整体保存/读取 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t confirmed;
    uint8_t reserved[3];
    competition_config_t config;
} competition_blob_t;

static SemaphoreHandle_t s_mutex;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_dirty;
static int64_t s_dirty_us;
static volatile bool s_last_ok;

static bool save_blob(void)
{
    line_params_t p;
    line_follow_get_params(&p);

    settings_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = SETTINGS_MAGIC;
    blob.version = SETTINGS_VERSION;
    blob.size = (uint16_t)sizeof(blob);
    blob.kp = p.kp;
    blob.ki = p.ki;
    blob.kd = p.kd;
    blob.threshold = p.threshold;
    blob.max_omega = p.max_omega;
    blob.black_high = p.black_high ? 1 : 0;
    blob.reverse = p.reverse ? 1 : 0;
    blob.accel = stepper_get_accel();
    chassis_get_speeds(&blob.chassis_sps, &blob.lift_sps);
    blob.proto = lu9685_get_protocol();
    blob.center[0] = lu9685_get_center(LU9685_CH_BASE);
    blob.center[1] = lu9685_get_center(LU9685_CH_TRAY);
    blob.center[2] = lu9685_get_center(LU9685_CH_GRIPPER);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, SETTINGS_KEY, &blob, sizeof(blob));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved: kp=%.2f kd=%.2f th=%.2f om=%.1f acc=%.0f",
                 blob.kp, blob.kd, blob.threshold, blob.max_omega, blob.accel);
    } else {
        ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(err));
    }
    s_last_ok = (err == ESP_OK);
    return err == ESP_OK;
}

esp_err_t settings_save_now(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    bool ok = save_blob();
    if (ok) {
        taskENTER_CRITICAL(&s_state_mux);
        s_dirty = false;
        taskEXIT_CRITICAL(&s_state_mux);
    }
    xSemaphoreGive(s_mutex);
    return ok ? ESP_OK : ESP_FAIL;
}

void settings_request_save(void)
{
    taskENTER_CRITICAL(&s_state_mux);
    s_dirty = true;
    s_dirty_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&s_state_mux);
}

bool settings_last_save_ok(void)
{
    return s_last_ok;
}

static void apply_blob(const settings_blob_t *blob)
{
    line_params_t p;
    line_follow_get_params(&p);
    p.kp = blob->kp;
    p.ki = blob->ki;
    p.kd = blob->kd;
    p.threshold = blob->threshold;
    p.max_omega = blob->max_omega;
    p.black_high = blob->black_high != 0;
    p.reverse = blob->reverse != 0;
    line_follow_set_params(&p);      /* 内部会拒绝 NaN 并限幅 */

    stepper_set_accel(blob->accel);
    chassis_set_speeds(blob->chassis_sps, blob->lift_sps);

    if (blob->proto <= LU9685_PROTO_PULSE_HI) {
        lu9685_set_protocol(blob->proto);
    }
    if (blob->center[0] <= 180) {
        lu9685_set_center(LU9685_CH_BASE, blob->center[0]);
    }
    if (blob->center[1] <= 180) {
        lu9685_set_center(LU9685_CH_TRAY, blob->center[1]);
    }
    if (blob->center[2] <= 180) {
        lu9685_set_center(LU9685_CH_GRIPPER, blob->center[2]);
    }
}

static esp_err_t load_and_apply(void)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    settings_blob_t blob;
    size_t length = sizeof(blob);
    err = nvs_get_blob(handle, SETTINGS_KEY, &blob, &length);
    nvs_close(handle);

    if (err != ESP_OK) {
        return err;
    }
    if (length == sizeof(blob) && blob.magic == SETTINGS_MAGIC && blob.version == 1) {
        /* v1 的速度/加速度是整步单位，直接沿用会慢 16 倍。
           PID、阈值、极性、舵机校准与单位无关，保留；其余回落到新默认。 */
        apply_blob(&blob);
        stepper_set_accel(STEPPER_DEFAULT_ACCEL);
        chassis_set_speeds(CHASSIS_BASE_SPEED_DEFAULT, LIFT_SPEED_DEFAULT);
        ESP_LOGI(TAG, "migrated v1 tuning: PID kept, accel/speeds reset for 1/%d microstep",
                 WHEEL_MICROSTEP);
        return ESP_OK;
    }
    if (length != sizeof(blob) || blob.magic != SETTINGS_MAGIC ||
        blob.version != SETTINGS_VERSION) {
        ESP_LOGW(TAG, "saved blob ignored (len=%u ver=%u)", (unsigned)length,
                 (unsigned)blob.version);
        return ESP_ERR_INVALID_STATE;
    }
    apply_blob(&blob);
    ESP_LOGI(TAG, "loaded: kp=%.2f kd=%.2f th=%.2f om=%.1f acc=%.0f",
             blob.kp, blob.kd, blob.threshold, blob.max_omega, blob.accel);
    return ESP_OK;
}

static void settings_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        bool dirty;
        int64_t since;
        taskENTER_CRITICAL(&s_state_mux);
        dirty = s_dirty;
        since = s_dirty_us;
        taskEXIT_CRITICAL(&s_state_mux);
        if (dirty && (esp_timer_get_time() - since) >= SETTINGS_DEBOUNCE_US) {
            settings_save_now();
        }
    }
}

esp_err_t settings_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = load_and_apply();
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no saved tuning, using defaults (%s)", esp_err_to_name(err));
    }

    BaseType_t ret = xTaskCreatePinnedToCore(settings_task, "settings", 4096,
                                             NULL, 2, NULL, 0);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t settings_save_competition(const competition_config_t *config, bool confirmed)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    competition_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = COMP_SETTINGS_MAGIC;
    blob.version = COMP_SETTINGS_VERSION;
    blob.size = (uint16_t)sizeof(blob);
    blob.confirmed = confirmed ? 1 : 0;
    blob.config = *config;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, SETTINGS_COMP_KEY, &blob, sizeof(blob));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved arm config: bstow=%u bpick=%u btray=%u bplace=%u "
                      "gopen=%u gclose=%u zpick=%ld",
                 (unsigned)blob.config.manipulator.base_stow,
                 (unsigned)blob.config.manipulator.base_pick,
                 (unsigned)blob.config.manipulator.base_tray,
                 (unsigned)blob.config.manipulator.base_place,
                 (unsigned)blob.config.manipulator.gripper_open,
                 (unsigned)blob.config.manipulator.gripper_closed,
                 (long)blob.config.manipulator.z_ground_pick);
    } else {
        ESP_LOGW(TAG, "save arm config failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t settings_load_competition(competition_config_t *config, bool *confirmed)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    competition_blob_t blob;
    size_t length = sizeof(blob);
    err = nvs_get_blob(handle, SETTINGS_COMP_KEY, &blob, &length);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }
    if (length != sizeof(blob) || blob.magic != COMP_SETTINGS_MAGIC ||
        blob.version != COMP_SETTINGS_VERSION) {
        ESP_LOGW(TAG, "arm config ignored (len=%u ver=%u)", (unsigned)length,
                 (unsigned)blob.version);
        return ESP_ERR_INVALID_STATE;
    }
    *config = blob.config;
    if (confirmed != NULL) {
        *confirmed = (blob.confirmed != 0);
    }
    ESP_LOGI(TAG, "loaded arm config: bstow=%u bpick=%u gopen=%u gclose=%u",
             (unsigned)config->manipulator.base_stow,
             (unsigned)config->manipulator.base_pick,
             (unsigned)config->manipulator.gripper_open,
             (unsigned)config->manipulator.gripper_closed);
    return ESP_OK;
}
