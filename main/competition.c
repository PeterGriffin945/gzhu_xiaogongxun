#include "competition.h"

#include <math.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "ads1115.h"
#include "app_config.h"
#include "chassis.h"
#include "line_follow.h"
#include "lu9685.h"
#include "stepper.h"

#define COMPETITION_PERIOD_MS 10
#define COMPETITION_RUN_LIMIT_MS 360000
#define COMPETITION_IDENTIFY_TIMEOUT_MS 12000
/* 升降速度最低可调到 100 步/秒，6000 步单程就要 60s；给整个取放动作留足余量。 */
#define COMPETITION_ACTION_TIMEOUT_MS 180000
#define COMPETITION_BRAKE_TIMEOUT_MS 1800
#define COMPETITION_BRAKE_MARGIN_MS 800
#define COMPETITION_SENSOR_TIMEOUT_MS 500

static SemaphoreHandle_t s_lock;
static competition_config_t s_config;
static competition_state_t s_state = COMPETITION_MANUAL;
static competition_error_t s_error = COMPETITION_ERROR_NONE;
static bool s_state_entry;
static bool s_running;
static bool s_config_confirmed;
static bool s_z_zero_valid;
static uint8_t s_picked_count;
static uint8_t s_placed_count;
static uint8_t s_align_iterations;
static bool s_align_moving;
static vision_color_t s_current_color;
static vision_color_t s_required_color;
static station_detector_t s_station;
static int64_t s_state_started_ms;
static int64_t s_run_started_ms;
static int64_t s_vision_after_ms;
static uint32_t s_sensor_seq;
static int64_t s_sensor_changed_ms;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void default_config(competition_config_t *config)
{
    *config = (competition_config_t) {
        .manipulator = {
            .z_transport = 0,
            .z_ground_pick = 6000,
            .z_tray = 2500,
            .z_ground_place = 6000,
            .z_max = 10000,
            .base_stow = 90,
            .base_pick = 0,
            .base_tray = 180,
            .base_place = 0,
            .gripper_open = 30,
            .gripper_closed = 80,
            .base_speed_angle = ARM_BASE_SPEED_ANGLE_DEFAULT,
            .base_sweep_ms = ARM_BASE_SWEEP_MS_DEFAULT,
            .base_settle_ms = 450,
            .gripper_settle_ms = 300,
            .lift_speed_sps = 400.0f,
        },
        .material_target_cx = 160,
        .material_target_cy = 120,
        .ring_target_cx = 160,
        .ring_target_cy = 120,
        .vision_tolerance_px = 8,
        .vision_min_confidence = 10,
        .max_align_iterations = 5,
        .vision_x_sign = 1,
        .vision_y_sign = 1,
        .align_nudge_mm = 3.0f,
        .align_speed_sps = 200.0f,
        .tray_speed_angle = 60,
        .tray_index_ms = 500,
    };
    station_detector_t detector;
    station_detector_init(&detector);
    config->station = detector.config;
}

static bool valid_config(const competition_config_t *config)
{
    const manipulator_config_t *m = &config->manipulator;
    if (m->z_transport != 0 || m->z_max < 100 || m->z_max > 100000 ||
        m->z_ground_pick < 0 || m->z_ground_pick > m->z_max ||
        m->z_tray < 0 || m->z_tray > m->z_max ||
        m->z_ground_place < 0 || m->z_ground_place > m->z_max ||
        m->base_stow > 180 || m->base_pick > 180 || m->base_tray > 180 ||
        m->base_place > 180 || m->gripper_open > 180 || m->gripper_closed > 180 ||
        m->gripper_open == m->gripper_closed || m->base_settle_ms > 5000 ||
        m->base_speed_angle < 10 || m->base_speed_angle > 170 ||
        m->base_sweep_ms < 200 || m->base_sweep_ms > 10000 ||
        m->gripper_settle_ms > 5000 || !isfinite(m->lift_speed_sps) ||
        m->lift_speed_sps < 100.0f || m->lift_speed_sps > 1200.0f) {
        return false;
    }
    if (config->material_target_cx >= 320 || config->ring_target_cx >= 320 ||
        config->material_target_cy >= 240 || config->ring_target_cy >= 240 ||
        config->vision_tolerance_px == 0 || config->vision_tolerance_px > 100 ||
        config->vision_min_confidence > 100 || config->max_align_iterations == 0 ||
        config->max_align_iterations > 20 ||
        (config->vision_x_sign != 1 && config->vision_x_sign != -1) ||
        (config->vision_y_sign != 1 && config->vision_y_sign != -1) ||
        !isfinite(config->align_nudge_mm) || !isfinite(config->align_speed_sps) ||
        /* 1 步 ≈ 1.02mm；小于 1mm 的微调会被四舍五入成 0 步并直接判失败 */
        config->align_nudge_mm < 1.0f || config->align_nudge_mm > 50.0f ||
        config->align_speed_sps < 50.0f || config->align_speed_sps > 1000.0f ||
        config->tray_speed_angle == 0 || config->tray_speed_angle >= 90 ||
        config->tray_index_ms < 20 || config->tray_index_ms > 10000) {
        return false;
    }
    const station_detector_config_t *station = &config->station;
    return station->min_black >= 2 && station->min_black <= 8 &&
           station->confirm_frames >= 1 && station->confirm_frames <= 10 &&
           station->clear_max_black < station->min_black &&
           station->rearm_frames >= 1 && station->rearm_frames <= 20 &&
           station->min_interval_ms <= 10000;
}

static void apply_config(void)
{
    manipulator_configure(&s_config.manipulator);
    tray_controller_configure(s_config.tray_speed_angle, s_config.tray_index_ms);
    s_station.config = s_config.station;
    /* 标定值直接变成升降软限位：开环升降没有反馈，越界只能靠这里挡住。 */
    chassis_set_lift_limits(0, s_config.manipulator.z_max);
}

static void change_state(competition_state_t state)
{
    s_state = state;
    s_state_started_ms = now_ms();
    s_state_entry = true;
}

static void abort_locked(competition_error_t error)
{
    s_running = false;
    s_error = error;
    s_state = COMPETITION_ABORTED;
    s_state_entry = false;
    s_z_zero_valid = false;
    manipulator_abort();
    chassis_stop();
    tray_controller_abort();
    lu9685_pulse_stop_all();
    k230_link_set_mode(VISION_MODE_IDLE);
}

static void finish_locked(void)
{
    s_running = false;
    s_error = COMPETITION_ERROR_NONE;
    s_state = COMPETITION_FINISHED;
    s_state_entry = false;
    chassis_stop();
    lu9685_pulse_stop_all();
    k230_link_set_mode(VISION_MODE_IDLE);
}

static bool enter_state(void)
{
    s_state_entry = false;
    esp_err_t err = ESP_OK;
    switch (s_state) {
        case COMPETITION_ARM_STOW:
            if (k230_link_set_mode(VISION_MODE_IDLE) != ESP_OK) {
                abort_locked(COMPETITION_ERROR_VISION_LINK);
                return false;
            }
            err = manipulator_start(MANIPULATOR_OP_STOW);
            break;
        case COMPETITION_SOURCE_TRAVEL:
        case COMPETITION_TARGET_TRAVEL:
            if (k230_link_set_mode(VISION_MODE_IDLE) != ESP_OK) {
                abort_locked(COMPETITION_ERROR_VISION_LINK);
                return false;
            }
            s_station.config = s_config.station;
            station_detector_reset(&s_station, now_ms());
            s_sensor_seq = ads1115_get_seq();
            s_sensor_changed_ms = now_ms();
            chassis_set_mode(CHASSIS_MODE_LINE);
            break;
        case COMPETITION_SOURCE_IDENTIFY:
            s_vision_after_ms = now_ms();
            if (k230_link_set_mode(VISION_MODE_MATERIAL) != ESP_OK) {
                abort_locked(COMPETITION_ERROR_VISION_LINK);
                return false;
            }
            break;
        case COMPETITION_TARGET_IDENTIFY:
            s_vision_after_ms = now_ms();
            if (k230_link_set_mode(VISION_MODE_RING) != ESP_OK) {
                abort_locked(COMPETITION_ERROR_VISION_LINK);
                return false;
            }
            break;
        case COMPETITION_SOURCE_ALIGN:
        case COMPETITION_TARGET_ALIGN:
            s_align_iterations = 0;
            s_align_moving = false;
            s_vision_after_ms = now_ms();
            break;
        case COMPETITION_SOURCE_PICK_LOAD:
            err = manipulator_start(MANIPULATOR_OP_PICK_AND_LOAD);
            break;
        case COMPETITION_SOURCE_INDEX:
            err = tray_controller_start_next();
            break;
        case COMPETITION_TARGET_SELECT: {
            int slot = tray_controller_find_color(s_required_color);
            if (slot < 0) {
                abort_locked(COMPETITION_ERROR_TRAY);
                return false;
            }
            err = tray_controller_start_to((uint8_t)slot);
            break;
        }
        case COMPETITION_TARGET_PLACE:
            err = manipulator_start(MANIPULATOR_OP_UNLOAD_AND_PLACE);
            break;
        default:
            break;
    }
    if (err != ESP_OK) {
        abort_locked(COMPETITION_ERROR_MANIPULATOR);
        return false;
    }
    return true;
}

static void update_travel(bool source)
{
    float volts[LINE_SENSOR_COUNT] = { 0 };
    uint32_t seq = 0;
    if (!ads1115_get_latest(volts, &seq)) {
        if (now_ms() - s_sensor_changed_ms > COMPETITION_SENSOR_TIMEOUT_MS) {
            abort_locked(COMPETITION_ERROR_SENSOR);
        }
        return;
    }
    if (seq != s_sensor_seq) {
        s_sensor_seq = seq;
        s_sensor_changed_ms = now_ms();
    } else if (now_ms() - s_sensor_changed_ms > COMPETITION_SENSOR_TIMEOUT_MS) {
        abort_locked(COMPETITION_ERROR_SENSOR);
        return;
    }

    uint8_t mask = 0;
    for (int i = 0; i < LINE_SENSOR_COUNT; ++i) {
        if (line_follow_is_black(volts[i])) {
            mask |= (1U << i);
        }
    }
    if (station_detector_update(&s_station, mask, seq, now_ms())) {
        chassis_stop();
        change_state(source ? COMPETITION_SOURCE_BRAKE : COMPETITION_TARGET_BRAKE);
    }
}

static void update_brake(bool source)
{
    bool stopped = true;
    float max_speed = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float speed = fabsf(stepper_get_speed((stepper_axis_t)i));
        max_speed = fmaxf(max_speed, speed);
        if (speed > 1.0f) {
            stopped = false;
        }
    }
    if (stopped) {
        change_state(source ? COMPETITION_SOURCE_IDENTIFY : COMPETITION_TARGET_IDENTIFY);
        return;
    }
    /* 刹车超时按当前加速度动态计算，低加速度（100 步/秒²）时 1.8s 远远不够。 */
    float accel = stepper_get_accel();
    if (accel < 1.0f) {
        accel = STEPPER_MIN_ACCEL;
    }
    int64_t allowance = (int64_t)(max_speed / accel * 1000.0f) + COMPETITION_BRAKE_MARGIN_MS;
    if (allowance < COMPETITION_BRAKE_TIMEOUT_MS) {
        allowance = COMPETITION_BRAKE_TIMEOUT_MS;
    }
    if (now_ms() - s_state_started_ms > allowance) {
        abort_locked(COMPETITION_ERROR_CHASSIS);
    }
}

static void update_identify(bool source)
{
    if (now_ms() - s_state_started_ms > COMPETITION_IDENTIFY_TIMEOUT_MS) {
        abort_locked(COMPETITION_ERROR_VISION_TIMEOUT);
        return;
    }
    vision_mode_t mode = source ? VISION_MODE_MATERIAL : VISION_MODE_RING;
    if (!k230_link_result_fresh(mode)) {
        return;
    }
    vision_result_t result;
    k230_link_get_result(&result);
    if (result.received_ms < s_vision_after_ms || !result.detected || !result.stable ||
        result.color == VISION_COLOR_UNKNOWN || result.confidence < s_config.vision_min_confidence) {
        return;
    }
    if (source) {
        s_current_color = result.color;
        change_state(COMPETITION_SOURCE_ALIGN);
    } else {
        s_required_color = result.color;
        change_state(COMPETITION_TARGET_ALIGN);
    }
}

static void update_alignment(bool source)
{
    if (now_ms() - s_state_started_ms > COMPETITION_IDENTIFY_TIMEOUT_MS * 2) {
        abort_locked(COMPETITION_ERROR_ALIGNMENT);
        return;
    }
    if (s_align_moving) {
        if (chassis_translation_active()) {
            return;
        }
        if (!chassis_translation_done()) {
            abort_locked(COMPETITION_ERROR_CHASSIS);
            return;
        }
        chassis_stop();
        s_align_moving = false;
        s_align_iterations++;
        s_vision_after_ms = now_ms();
        return;
    }

    vision_mode_t mode = source ? VISION_MODE_MATERIAL : VISION_MODE_RING;
    if (!k230_link_result_fresh(mode)) {
        return;
    }
    vision_result_t result;
    k230_link_get_result(&result);
    vision_color_t expected = source ? s_current_color : s_required_color;
    if (result.received_ms < s_vision_after_ms || !result.detected || !result.stable ||
        result.color != expected || result.confidence < s_config.vision_min_confidence) {
        return;
    }

    int target_cx = source ? s_config.material_target_cx : s_config.ring_target_cx;
    int target_cy = source ? s_config.material_target_cy : s_config.ring_target_cy;
    int error_x = (int)result.cx - target_cx;
    int error_y = (int)result.cy - target_cy;
    int tolerance = s_config.vision_tolerance_px;
    if (abs(error_x) <= tolerance && abs(error_y) <= tolerance) {
        change_state(source ? COMPETITION_SOURCE_PICK_LOAD : COMPETITION_TARGET_SELECT);
        return;
    }
    if (s_align_iterations >= s_config.max_align_iterations) {
        abort_locked(COMPETITION_ERROR_ALIGNMENT);
        return;
    }

    float dx = 0.0f;
    float dy = 0.0f;
    if (abs(error_x) > tolerance) {
        dx = ((error_x > 0) ? s_config.align_nudge_mm : -s_config.align_nudge_mm) *
             s_config.vision_x_sign;
    }
    if (abs(error_y) > tolerance) {
        dy = ((error_y > 0) ? s_config.align_nudge_mm : -s_config.align_nudge_mm) *
             s_config.vision_y_sign;
    }
    esp_err_t err = chassis_start_translation(dx, dy, s_config.align_speed_sps);
    if (err != ESP_OK) {
        abort_locked(COMPETITION_ERROR_CHASSIS);
        return;
    }
    s_align_moving = true;
}

static void update_manipulator_state(bool source)
{
    manipulator_status_t status = manipulator_update();
    if (status == MANIPULATOR_RUNNING) {
        return;
    }
    if (status == MANIPULATOR_ERROR) {
        abort_locked(COMPETITION_ERROR_MANIPULATOR);
        return;
    }
    if (status != MANIPULATOR_DONE) {
        return;
    }

    tray_status_t tray;
    tray_controller_get_status(&tray);
    if (source) {
        if (tray_controller_mark_loaded(tray.current_slot, s_current_color) != ESP_OK) {
            abort_locked(COMPETITION_ERROR_TRAY);
            return;
        }
        s_picked_count++;
        change_state((s_picked_count < 3) ? COMPETITION_SOURCE_INDEX : COMPETITION_TARGET_TRAVEL);
    } else {
        tray_controller_mark_empty(tray.current_slot);
        s_placed_count++;
        if (s_placed_count >= 3) {
            finish_locked();
        } else {
            change_state(COMPETITION_TARGET_TRAVEL);
        }
    }
}

static void update_tray_state(bool source)
{
    tray_status_t tray;
    tray_controller_get_status(&tray);
    if (tray.motion == TRAY_MOTION_ERROR) {
        abort_locked(COMPETITION_ERROR_TRAY);
        return;
    }
    if (!tray_controller_busy() && tray.motion == TRAY_MOTION_DONE) {
        change_state(source ? COMPETITION_SOURCE_TRAVEL : COMPETITION_TARGET_PLACE);
    }
}

static void competition_update(void)
{
    tray_controller_update();
    if (!s_running) {
        return;
    }
    int64_t current = now_ms();
    if (current - s_run_started_ms >= COMPETITION_RUN_LIMIT_MS) {
        abort_locked(COMPETITION_ERROR_TIMEOUT);
        return;
    }
    if (!k230_link_online()) {
        abort_locked(COMPETITION_ERROR_VISION_LINK);
        return;
    }
    if (lu9685_pulse_faulted(LU9685_CH_TRAY)) {
        abort_locked(COMPETITION_ERROR_SERVO);
        return;
    }
    if (s_state_entry && !enter_state()) {
        return;
    }

    switch (s_state) {
        case COMPETITION_ARM_STOW:
            if (manipulator_update() == MANIPULATOR_ERROR) {
                abort_locked(COMPETITION_ERROR_MANIPULATOR);
            } else if (!manipulator_busy()) {
                change_state(COMPETITION_SOURCE_TRAVEL);
            }
            break;
        case COMPETITION_SOURCE_TRAVEL:
            update_travel(true);
            break;
        case COMPETITION_TARGET_TRAVEL:
            update_travel(false);
            break;
        case COMPETITION_SOURCE_BRAKE:
            update_brake(true);
            break;
        case COMPETITION_TARGET_BRAKE:
            update_brake(false);
            break;
        case COMPETITION_SOURCE_IDENTIFY:
            update_identify(true);
            break;
        case COMPETITION_TARGET_IDENTIFY:
            update_identify(false);
            break;
        case COMPETITION_SOURCE_ALIGN:
            update_alignment(true);
            break;
        case COMPETITION_TARGET_ALIGN:
            update_alignment(false);
            break;
        case COMPETITION_SOURCE_PICK_LOAD:
            if (current - s_state_started_ms > COMPETITION_ACTION_TIMEOUT_MS) {
                abort_locked(COMPETITION_ERROR_TIMEOUT);
            } else {
                update_manipulator_state(true);
            }
            break;
        case COMPETITION_TARGET_PLACE:
            if (current - s_state_started_ms > COMPETITION_ACTION_TIMEOUT_MS) {
                abort_locked(COMPETITION_ERROR_TIMEOUT);
            } else {
                update_manipulator_state(false);
            }
            break;
        case COMPETITION_SOURCE_INDEX:
            update_tray_state(true);
            break;
        case COMPETITION_TARGET_SELECT:
            update_tray_state(false);
            break;
        default:
            break;
    }
}

static void competition_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(COMPETITION_PERIOD_MS));
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
            competition_update();
            xSemaphoreGive(s_lock);
        }
    }
}

esp_err_t competition_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    default_config(&s_config);
    s_state_started_ms = now_ms();   /* 未开赛时 state_ms 不应显示开机时长 */
    s_run_started_ms = 0;
    manipulator_init();
    tray_controller_init();
    station_detector_init(&s_station);
    apply_config();
    BaseType_t ret = xTaskCreatePinnedToCore(competition_task, "competition", 8192, NULL, 3, NULL, 1);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t competition_start(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    tray_status_t tray;
    tray_controller_get_status(&tray);
    float volts[LINE_SENSOR_COUNT];
    uint32_t seq;
    if (s_running) {
        result = ESP_ERR_INVALID_STATE;
    } else if (!s_config_confirmed || !s_z_zero_valid || !tray.zero_valid) {
        result = ESP_ERR_INVALID_STATE;
    } else if (!k230_link_online() || !lu9685_available() || !lu9685_pulse_ready() ||
               lu9685_pulse_faulted(LU9685_CH_TRAY)) {
        result = ESP_ERR_INVALID_STATE;
    } else if (!stepper_chassis_enabled() || !stepper_lift_enabled() ||
               !ads1115_get_latest(volts, &seq)) {
        result = ESP_ERR_INVALID_STATE;
    } else if (tray_controller_busy() || manipulator_busy() ||
               chassis_translation_active() || !chassis_lift_idle() ||
               lu9685_pulse_active(LU9685_CH_BASE) ||
               lu9685_pulse_active(LU9685_CH_TRAY) ||
               lu9685_pulse_active(LU9685_CH_GRIPPER)) {
        /* 启动前必须所有执行器静止：手动循迹模式残留的轮速会在收臂阶段把车带跑。 */
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        chassis_stop();          /* 强制回 MANUAL 并平滑停车，再开始收臂 */
        /* 约定：启动前人工把底座摆到运输角，这里把它声明为逻辑基准。 */
        lu9685_set_target_angle(LU9685_CH_BASE, s_config.manipulator.base_stow);
        apply_config();
        s_error = COMPETITION_ERROR_NONE;
        s_picked_count = 0;
        s_placed_count = 0;
        s_current_color = VISION_COLOR_UNKNOWN;
        s_required_color = VISION_COLOR_UNKNOWN;
        s_run_started_ms = now_ms();
        s_running = true;
        change_state(COMPETITION_ARM_STOW);
    }
    xSemaphoreGive(s_lock);
    return result;
}

void competition_abort(competition_error_t error)
{
    /* 中止必须可靠落锁：锁持有者最多阻塞在几次 20ms I2C 写上，等待有限。
       之前用短超时会导致 running/state 未更新、舵机未被中止。 */
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        abort_locked(error);
        xSemaphoreGive(s_lock);
    }
}

bool competition_auto_active(void)
{
    return s_running;
}

void competition_get_status(competition_status_t *out)
{
    if (out == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    *out = (competition_status_t) {
        .state = s_state,
        .error = s_error,
        .running = s_running,
        .config_confirmed = s_config_confirmed,
        .z_zero_valid = s_z_zero_valid,
        .picked_count = s_picked_count,
        .placed_count = s_placed_count,
        .align_iterations = s_align_iterations,
        .current_color = s_current_color,
        .required_color = s_required_color,
        .z_position = stepper_get_position(AXIS_LIFT),
        .state_elapsed_ms = now_ms() - s_state_started_ms,
        .run_elapsed_ms = s_run_started_ms > 0 ? now_ms() - s_run_started_ms : 0,
    };
    tray_controller_get_status(&out->tray);
    k230_link_get_status(&out->link);
    k230_link_get_result(&out->vision);
    xSemaphoreGive(s_lock);
}

void competition_get_config(competition_config_t *out)
{
    if (out != NULL && xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        *out = s_config;
        xSemaphoreGive(s_lock);
    }
}

esp_err_t competition_set_config(const competition_config_t *config, bool confirm)
{
    if (config == NULL || !valid_config(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    if (s_running || tray_controller_busy()) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        s_config = *config;
        s_config_confirmed = confirm;
        apply_config();
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t competition_confirm_config(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    if (s_running || !valid_config(&s_config)) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        s_config_confirmed = true;
        apply_config();
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t competition_set_z_zero(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
    /* 必须完全静止（含尚未生效的 jog 指令）才能取零点。 */
    if (s_running || !chassis_lift_idle()) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        result = stepper_set_position(AXIS_LIFT, 0);
        if (result == ESP_OK) {
            s_z_zero_valid = true;
        }
    }
    xSemaphoreGive(s_lock);
    return result;
}

const char *competition_state_name(competition_state_t state)
{
    static const char *const names[] = {
        "MANUAL", "ARM_STOW", "SOURCE_TRAVEL", "SOURCE_BRAKE",
        "SOURCE_IDENTIFY", "SOURCE_ALIGN", "SOURCE_PICK_LOAD", "SOURCE_INDEX",
        "TARGET_TRAVEL", "TARGET_BRAKE", "TARGET_IDENTIFY", "TARGET_ALIGN",
        "TARGET_SELECT", "TARGET_PLACE", "FINISHED", "ABORTED",
    };
    return (state <= COMPETITION_ABORTED) ? names[state] : "UNKNOWN";
}

const char *competition_error_name(competition_error_t error)
{
    static const char *const names[] = {
        "NONE", "CALIBRATION", "VISION_LINK", "VISION_TIMEOUT", "ALIGNMENT",
        "TRAY", "MANIPULATOR", "CHASSIS", "SENSOR", "SERVO", "TIMEOUT", "MANUAL_STOP",
    };
    return (error <= COMPETITION_ERROR_MANUAL_STOP) ? names[error] : "UNKNOWN";
}
