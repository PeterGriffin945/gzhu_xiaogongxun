/**
 * @file safety.c
 * @brief 全局急停实现
 *
 * 急停不改变 EN 电平（升降失能可能掉件），只让所有运动立即归零：
 *  - 底盘/升降目标清零并直接清空步进相位（立即停止发脉冲）
 *  - 比赛状态机中止（若正在运行）
 *  - 舵机：取消所有定时点动；位置舵机保持当前角度
 */
#include "safety.h"

#include "esp_log.h"

#include "chassis.h"
#include "competition.h"
#include "lu9685.h"
#include "stepper.h"
#include "tray_controller.h"

static const char *TAG = "safety";

static volatile bool s_stopped;

void safety_init(void)
{
    s_stopped = false;
}

bool safety_is_stopped(void)
{
    return s_stopped;
}

void safety_emergency_stop(void)
{
    s_stopped = true;

    if (competition_auto_active()) {
        competition_abort(COMPETITION_ERROR_MANUAL_STOP);
    } else {
        chassis_stop();
        tray_controller_abort();
        lu9685_pulse_stop_all();
    }

    chassis_set_velocity(0.0f, 0.0f, 0.0f);
    chassis_lift_jog(0.0f);
    stepper_abort_all();      /* 立即停脉冲，不等待减速 */
    lu9685_hold_all();        /* 取消所有定时器，位置舵机保持 */

    ESP_LOGW(TAG, "EMERGENCY STOP engaged");
}

void safety_clear(void)
{
    s_stopped = false;
    ESP_LOGW(TAG, "emergency stop cleared; commands re-enabled");
}
