/**
 * @file settings.h
 * @brief 调参参数持久化（NVS）：循迹 PID、阈值/极性、加速度、底盘/升降速度、
 *        舵机协议与中位校准。上电自动加载，网页修改后自动防抖保存。
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "competition.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建互斥锁、尝试从 NVS 加载并应用到各模块、启动防抖保存任务 */
esp_err_t settings_init(void);

/** 标记参数已变化，约 2 秒无新修改后自动写入 NVS */
void settings_request_save(void);

/** 立即保存（网页“保存参数”按钮） */
esp_err_t settings_save_now(void);

/** 最近一次保存是否成功（供状态/日志使用） */
bool settings_last_save_ok(void);

/**
 * @brief 保存/读取机械臂与舵机标定（competition_config_t）
 *
 * 包含底座各动作角度、夹爪开/合角度、各步骤 Z 高度、速度角/行程、
 * 横线参数与确认状态。上电时由 main 调回并应用，动作脚本直接使用。
 */
esp_err_t settings_save_competition(const competition_config_t *config, bool confirmed);
esp_err_t settings_load_competition(competition_config_t *config, bool *confirmed);

#ifdef __cplusplus
}
#endif
