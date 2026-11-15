#include "tray_controller.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#include "app_config.h"
#include "lu9685.h"

#define TRAY_SETTLE_MS 200
#define TRAY_DEFAULT_SPEED_ANGLE 60
#define TRAY_DEFAULT_INDEX_MS 500
#define TRAY_LOCK_TIMEOUT_MS 200

/* 比赛任务（Core1）与 HTTP 手动命令（Core0）都会改状态，必须串行化。 */
static SemaphoreHandle_t s_mux;
static tray_status_t s_tray;
static bool s_settling;
static int64_t s_settle_until_ms;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool tray_lock(void)
{
    if (s_mux == NULL) {
        return true;   /* 初始化失败时退化为无锁，至少保留手动功能 */
    }
    return xSemaphoreTakeRecursive(s_mux, pdMS_TO_TICKS(TRAY_LOCK_TIMEOUT_MS)) == pdTRUE;
}

static void tray_unlock(void)
{
    if (s_mux != NULL) {
        xSemaphoreGiveRecursive(s_mux);
    }
}

static bool busy_locked(void)
{
    return s_tray.motion == TRAY_MOTION_RUNNING;
}

static esp_err_t start_pulse_locked(void)
{
    s_settling = false;
    esp_err_t err = lu9685_pulse_start(LU9685_CH_TRAY, s_tray.speed_angle,
                                       s_tray.index_duration_ms, -1);
    if (err != ESP_OK) {
        s_tray.motion = TRAY_MOTION_ERROR;
        s_tray.last_error = err;
    }
    return err;
}

static esp_err_t start_to_locked(uint8_t target_slot)
{
    if (!s_tray.zero_valid || target_slot >= TRAY_SLOT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (busy_locked()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_tray.target_slot = target_slot;
    s_tray.last_error = ESP_OK;
    if (s_tray.current_slot == target_slot) {
        s_tray.motion = TRAY_MOTION_DONE;
        return ESP_OK;
    }
    s_tray.motion = TRAY_MOTION_RUNNING;
    return start_pulse_locked();
}

void tray_controller_init(void)
{
    s_mux = xSemaphoreCreateRecursiveMutex();
    s_tray = (tray_status_t) {
        .zero_valid = false,
        .current_slot = 0,
        .target_slot = 0,
        .speed_angle = TRAY_DEFAULT_SPEED_ANGLE,
        .index_duration_ms = TRAY_DEFAULT_INDEX_MS,
        .motion = TRAY_MOTION_IDLE,
        .last_error = ESP_OK,
    };
    for (int i = 0; i < TRAY_SLOT_COUNT; ++i) {
        s_tray.colors[i] = VISION_COLOR_UNKNOWN;
    }
    s_settling = false;
}

esp_err_t tray_controller_set_zero(void)
{
    if (!tray_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    if (busy_locked() || lu9685_pulse_active(LU9685_CH_TRAY)) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        s_tray.zero_valid = true;
        s_tray.current_slot = 0;
        s_tray.target_slot = 0;
        s_tray.motion = TRAY_MOTION_IDLE;
        s_tray.last_error = ESP_OK;
        for (int i = 0; i < TRAY_SLOT_COUNT; ++i) {
            s_tray.colors[i] = VISION_COLOR_UNKNOWN;
        }
    }
    tray_unlock();
    return result;
}

void tray_controller_invalidate(void)
{
    if (tray_lock()) {
        s_tray.zero_valid = false;
        tray_unlock();
    }
}

esp_err_t tray_controller_configure(uint8_t speed_angle, uint32_t index_duration_ms)
{
    if (!tray_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    if (busy_locked() || speed_angle == 0 || speed_angle >= 90 ||
        index_duration_ms < 20 || index_duration_ms > 10000) {
        result = ESP_ERR_INVALID_ARG;
    } else {
        s_tray.speed_angle = speed_angle;
        s_tray.index_duration_ms = index_duration_ms;
    }
    tray_unlock();
    return result;
}

esp_err_t tray_controller_start_next(void)
{
    if (!tray_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result;
    if (!s_tray.zero_valid) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        result = start_to_locked((uint8_t)((s_tray.current_slot + 1) % TRAY_SLOT_COUNT));
    }
    tray_unlock();
    return result;
}

esp_err_t tray_controller_start_to(uint8_t target_slot)
{
    if (!tray_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = start_to_locked(target_slot);
    tray_unlock();
    return result;
}

void tray_controller_update(void)
{
    if (!tray_lock()) {
        return;
    }
    if (s_tray.motion != TRAY_MOTION_RUNNING) {
        tray_unlock();
        return;
    }
    if (lu9685_pulse_faulted(LU9685_CH_TRAY)) {
        s_tray.motion = TRAY_MOTION_ERROR;
        s_tray.last_error = ESP_FAIL;
        s_tray.zero_valid = false;
        tray_unlock();
        return;
    }
    if (lu9685_pulse_active(LU9685_CH_TRAY)) {
        tray_unlock();
        return;
    }
    if (!s_settling) {
        s_settling = true;
        s_settle_until_ms = now_ms() + TRAY_SETTLE_MS;
        tray_unlock();
        return;
    }
    if (now_ms() < s_settle_until_ms) {
        tray_unlock();
        return;
    }

    s_settling = false;
    s_tray.current_slot = (uint8_t)((s_tray.current_slot + 1) % TRAY_SLOT_COUNT);
    if (s_tray.current_slot == s_tray.target_slot) {
        s_tray.motion = TRAY_MOTION_DONE;
    } else {
        start_pulse_locked();
    }
    tray_unlock();
}

void tray_controller_abort(void)
{
    if (!tray_lock()) {
        return;
    }
    lu9685_pulse_cancel(LU9685_CH_TRAY);
    s_settling = false;
    s_tray.motion = TRAY_MOTION_IDLE;
    s_tray.zero_valid = false;
    tray_unlock();
}

bool tray_controller_busy(void)
{
    if (!tray_lock()) {
        return false;
    }
    bool busy = busy_locked();
    tray_unlock();
    return busy;
}

void tray_controller_get_status(tray_status_t *out)
{
    if (out == NULL) {
        return;
    }
    if (!tray_lock()) {
        return;
    }
    *out = s_tray;
    tray_unlock();
}

esp_err_t tray_controller_mark_loaded(uint8_t slot, vision_color_t color)
{
    if (slot >= TRAY_SLOT_COUNT || color <= VISION_COLOR_UNKNOWN || color > VISION_COLOR_BLUE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!tray_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    if (!s_tray.zero_valid) {
        result = ESP_ERR_INVALID_ARG;
    } else {
        s_tray.colors[slot] = color;
    }
    tray_unlock();
    return result;
}

esp_err_t tray_controller_mark_empty(uint8_t slot)
{
    if (slot >= TRAY_SLOT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!tray_lock()) {
        return ESP_ERR_TIMEOUT;
    }
    s_tray.colors[slot] = VISION_COLOR_UNKNOWN;
    tray_unlock();
    return ESP_OK;
}

int tray_controller_find_color(vision_color_t color)
{
    if (color <= VISION_COLOR_UNKNOWN || color > VISION_COLOR_BLUE) {
        return -1;
    }
    if (!tray_lock()) {
        return -1;
    }
    int found = -1;
    if (s_tray.zero_valid) {
        for (int i = 0; i < TRAY_SLOT_COUNT; ++i) {
            if (s_tray.colors[i] == color) {
                found = i;
                break;
            }
        }
    }
    tray_unlock();
    return found;
}

uint8_t tray_forward_steps(uint8_t current_slot, uint8_t target_slot)
{
    if (current_slot >= TRAY_SLOT_COUNT || target_slot >= TRAY_SLOT_COUNT) {
        return 0xFF;
    }
    return (uint8_t)((target_slot + TRAY_SLOT_COUNT - current_slot) % TRAY_SLOT_COUNT);
}
