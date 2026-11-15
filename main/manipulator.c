#include "manipulator.h"

#include <stddef.h>

#include "esp_timer.h"
#include "app_config.h"
#include "chassis.h"
#include "lu9685.h"
#include "stepper.h"

typedef enum {
    ACTION_BASE_STOW = 0,
    ACTION_BASE_PICK,
    ACTION_BASE_TRAY,
    ACTION_BASE_PLACE,
    ACTION_GRIPPER_OPEN,
    ACTION_GRIPPER_CLOSED,
    ACTION_Z_TRANSPORT,
    ACTION_Z_GROUND_PICK,
    ACTION_Z_TRAY,
    ACTION_Z_GROUND_PLACE,
} manipulator_action_t;

static const manipulator_action_t s_stow_script[] = {
    ACTION_GRIPPER_OPEN, ACTION_Z_TRANSPORT, ACTION_BASE_STOW,
};
static const manipulator_action_t s_pick_load_script[] = {
    ACTION_GRIPPER_OPEN, ACTION_BASE_PICK, ACTION_Z_GROUND_PICK,
    ACTION_GRIPPER_CLOSED, ACTION_Z_TRANSPORT, ACTION_BASE_TRAY,
    ACTION_Z_TRAY, ACTION_GRIPPER_OPEN, ACTION_Z_TRANSPORT, ACTION_BASE_STOW,
};
static const manipulator_action_t s_unload_place_script[] = {
    ACTION_BASE_TRAY, ACTION_Z_TRAY, ACTION_GRIPPER_CLOSED,
    ACTION_Z_TRANSPORT, ACTION_BASE_PLACE, ACTION_Z_GROUND_PLACE,
    ACTION_GRIPPER_OPEN, ACTION_Z_TRANSPORT, ACTION_BASE_STOW,
};

static manipulator_config_t s_config;
static manipulator_status_t s_status;
static esp_err_t s_last_error;
static const manipulator_action_t *s_script;
static uint8_t s_script_length;
static uint8_t s_step;
static bool s_step_started;
static bool s_wait_servo;
static int64_t s_wait_until_ms;
static int32_t s_z_target;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void manipulator_init(void)
{
    s_status = MANIPULATOR_IDLE;
    s_last_error = ESP_OK;
}

void manipulator_configure(const manipulator_config_t *config)
{
    if (config != NULL && s_status != MANIPULATOR_RUNNING) {
        s_config = *config;
    }
}

esp_err_t manipulator_start(manipulator_operation_t operation)
{
    if (s_status == MANIPULATOR_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    switch (operation) {
        case MANIPULATOR_OP_STOW:
            s_script = s_stow_script;
            s_script_length = sizeof(s_stow_script) / sizeof(s_stow_script[0]);
            break;
        case MANIPULATOR_OP_PICK_AND_LOAD:
            s_script = s_pick_load_script;
            s_script_length = sizeof(s_pick_load_script) / sizeof(s_pick_load_script[0]);
            break;
        case MANIPULATOR_OP_UNLOAD_AND_PLACE:
            s_script = s_unload_place_script;
            s_script_length = sizeof(s_unload_place_script) / sizeof(s_unload_place_script[0]);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    s_step = 0;
    s_step_started = false;
    s_wait_servo = false;
    s_last_error = ESP_OK;
    s_status = MANIPULATOR_RUNNING;
    return ESP_OK;
}

static esp_err_t start_servo(uint8_t channel, uint8_t angle, uint16_t settle_ms)
{
    esp_err_t err = lu9685_set_angle(channel, angle);
    if (err == ESP_OK) {
        s_wait_servo = true;
        s_wait_until_ms = now_ms() + settle_ms;
    }
    return err;
}

/* CH0 底座是连续旋转舵机：按角度差换算转动时间，到点自动停在中位。 */
static esp_err_t start_base(uint8_t angle, uint16_t settle_ms)
{
    uint32_t duration = 0;
    esp_err_t err = lu9685_move_timed(LU9685_CH_BASE, angle,
                                      s_config.base_speed_angle,
                                      s_config.base_sweep_ms,
                                      -1, &duration);
    if (err == ESP_OK) {
        s_wait_servo = true;
        s_wait_until_ms = now_ms() + duration + settle_ms;
    }
    return err;
}

static esp_err_t start_action(manipulator_action_t action)
{
    s_wait_servo = false;
    switch (action) {
        case ACTION_BASE_STOW:
            return start_base(s_config.base_stow, s_config.base_settle_ms);
        case ACTION_BASE_PICK:
            return start_base(s_config.base_pick, s_config.base_settle_ms);
        case ACTION_BASE_TRAY:
            return start_base(s_config.base_tray, s_config.base_settle_ms);
        case ACTION_BASE_PLACE:
            return start_base(s_config.base_place, s_config.base_settle_ms);
        case ACTION_GRIPPER_OPEN:
            return start_servo(LU9685_CH_GRIPPER, s_config.gripper_open, s_config.gripper_settle_ms);
        case ACTION_GRIPPER_CLOSED:
            return start_servo(LU9685_CH_GRIPPER, s_config.gripper_closed, s_config.gripper_settle_ms);
        case ACTION_Z_TRANSPORT:
            s_z_target = s_config.z_transport;
            break;
        case ACTION_Z_GROUND_PICK:
            s_z_target = s_config.z_ground_pick;
            break;
        case ACTION_Z_TRAY:
            s_z_target = s_config.z_tray;
            break;
        case ACTION_Z_GROUND_PLACE:
            s_z_target = s_config.z_ground_place;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return chassis_lift_move_to(s_z_target, s_config.lift_speed_sps);
}

static void advance_step(void)
{
    s_step++;
    s_step_started = false;
    s_wait_servo = false;
    if (s_step >= s_script_length) {
        s_status = MANIPULATOR_DONE;
    }
}

manipulator_status_t manipulator_update(void)
{
    if (s_status != MANIPULATOR_RUNNING) {
        return s_status;
    }
    if (!s_step_started) {
        esp_err_t err = start_action(s_script[s_step]);
        if (err != ESP_OK) {
            s_last_error = err;
            s_status = MANIPULATOR_ERROR;
            return s_status;
        }
        s_step_started = true;
    }

    if (s_wait_servo) {
        if (now_ms() >= s_wait_until_ms) {
            advance_step();
        }
        return s_status;
    }

    if (chassis_lift_move_active()) {
        return s_status;
    }
    if (stepper_get_position(AXIS_LIFT) == s_z_target) {
        advance_step();
    } else if (stepper_get_move_status(AXIS_LIFT) != STEPPER_MOVE_RUNNING) {
        s_last_error = ESP_FAIL;
        s_status = MANIPULATOR_ERROR;
    }
    return s_status;
}

bool manipulator_busy(void)
{
    return s_status == MANIPULATOR_RUNNING;
}

esp_err_t manipulator_last_error(void)
{
    return s_last_error;
}

void manipulator_abort(void)
{
    if (s_status == MANIPULATOR_RUNNING) {
        s_status = MANIPULATOR_IDLE;
    }
    s_step_started = false;
    s_wait_servo = false;
}

uint8_t manipulator_current_step(void)
{
    return s_step;
}
