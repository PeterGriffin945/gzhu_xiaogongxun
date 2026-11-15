/**
 * @file settings.h
 * @brief 调参参数持久化（NVS）：循迹 PID、阈值/极性、加速度、底盘/升降速度、
 *        舵机协议与中位校准。上电自动加载，网页修改后自动防抖保存。
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

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

#ifdef __cplusplus
}
#endif
