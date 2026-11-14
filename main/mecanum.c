/**
 * @file mecanum.c
 * @brief O 型麦克纳姆轮逆运动学实现
 *
 *           前进 vy
 *            ↑
 *   LF ●─────┼─────● RF
 *      │     │     │
 *   LR ●─────┴─────● RR   → 右移 vx
 *
 *   LF = vy + vx - k·ω
 *   RF = vy - vx + k·ω
 *   LR = vy - vx - k·ω
 *   RR = vy + vx + k·ω
 *   k  = (轴距/2 + 轮距/2)
 */
#include "mecanum.h"

#include <math.h>
#include <stddef.h>

#include "app_config.h"

static float half_diagonal_m(void)
{
    return ((WHEELBASE_MM + TRACK_MM) * 0.5f) * 0.001f;
}

static float wheel_circumference_m(void)
{
    return (float)M_PI * (WHEEL_DIAMETER_MM * 0.001f);
}

void mecanum_inverse(const chassis_vel_t *cmd, float wheel_mps[4])
{
    if (cmd == NULL || wheel_mps == NULL) {
        return;
    }

    const float k = half_diagonal_m();
    const float vx = cmd->vx;
    const float vy = cmd->vy;
    const float w  = cmd->omega * k;

    wheel_mps[0] = vy + vx + w;   /* RR */
    wheel_mps[1] = vy - vx + w;   /* RF */
    wheel_mps[2] = vy + vx - w;   /* LF */
    wheel_mps[3] = vy - vx - w;   /* LR */

    const float limit = mecanum_max_wheel_mps();
    for (int i = 0; i < 4; ++i) {
        if (wheel_mps[i] > limit) {
            wheel_mps[i] = limit;
        } else if (wheel_mps[i] < -limit) {
            wheel_mps[i] = -limit;
        }
    }
}

int32_t mecanum_mps_to_steps(float mps)
{
    const float steps_per_meter =
        (float)WHEEL_STEPS_PER_REV / wheel_circumference_m();
    float steps = mps * steps_per_meter;
    if (steps > (float)STEPPER_MAX_SPEED) {
        steps = (float)STEPPER_MAX_SPEED;
    } else if (steps < -(float)STEPPER_MAX_SPEED) {
        steps = -(float)STEPPER_MAX_SPEED;
    }
    return (int32_t)lroundf(steps);
}

float mecanum_steps_to_mps(int32_t steps)
{
    const float steps_per_meter =
        (float)WHEEL_STEPS_PER_REV / wheel_circumference_m();
    return (float)steps / steps_per_meter;
}

float mecanum_max_wheel_mps(void)
{
    const float steps_per_meter =
        (float)WHEEL_STEPS_PER_REV / wheel_circumference_m();
    return STEPPER_MAX_SPEED / steps_per_meter;
}
