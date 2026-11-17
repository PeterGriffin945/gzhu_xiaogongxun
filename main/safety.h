/**
 * @file safety.h
 * @brief 全局急停锁：立即停止所有执行器，解除后恢复手动控制
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void safety_init(void);

/**
 * @brief 急停（锁存）
 *
 * 立即：清零底盘/升降、取消所有舵机定时、中止比赛、清空步进相位。
 * 急停期间所有运动类 HTTP 命令返回 423，必须显式解除。
 */
void safety_emergency_stop(void);

/** 解除急停锁存。不会自动恢复运动，需重新下发命令。 */
void safety_clear(void);

/** 是否处于急停锁存状态（无锁读取，可被 HTTP 直接调用）。 */
bool safety_is_stopped(void);

#ifdef __cplusplus
}
#endif
