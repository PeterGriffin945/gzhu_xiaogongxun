#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "k230_link.h"
#include "manipulator.h"
#include "station_detector.h"
#include "tray_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COMPETITION_MANUAL = 0,
    COMPETITION_ARM_STOW,
    COMPETITION_SOURCE_TRAVEL,
    COMPETITION_SOURCE_BRAKE,
    COMPETITION_SOURCE_IDENTIFY,
    COMPETITION_SOURCE_ALIGN,
    COMPETITION_SOURCE_PICK_LOAD,
    COMPETITION_SOURCE_INDEX,
    COMPETITION_TARGET_TRAVEL,
    COMPETITION_TARGET_BRAKE,
    COMPETITION_TARGET_IDENTIFY,
    COMPETITION_TARGET_ALIGN,
    COMPETITION_TARGET_SELECT,
    COMPETITION_TARGET_PLACE,
    COMPETITION_BLIND_MOVE,    /* 离开/返回黑线的定步盲走 */
    COMPETITION_DEBUG_PAUSE,   /* 逢横线即停：等待 Web 人工抓取 */
    COMPETITION_FINISHED,
    COMPETITION_ABORTED,
} competition_state_t;

typedef enum {
    COMPETITION_ERROR_NONE = 0,
    COMPETITION_ERROR_CALIBRATION,
    COMPETITION_ERROR_VISION_LINK,
    COMPETITION_ERROR_VISION_TIMEOUT,
    COMPETITION_ERROR_ALIGNMENT,
    COMPETITION_ERROR_TRAY,
    COMPETITION_ERROR_MANIPULATOR,
    COMPETITION_ERROR_CHASSIS,
    COMPETITION_ERROR_SENSOR,
    COMPETITION_ERROR_SERVO,
    COMPETITION_ERROR_TIMEOUT,
    COMPETITION_ERROR_MANUAL_STOP,
} competition_error_t;

typedef struct {
    manipulator_config_t manipulator;
    uint16_t material_target_cx;
    uint16_t material_target_cy;
    uint16_t ring_target_cx;
    uint16_t ring_target_cy;
    uint16_t vision_tolerance_px;
    uint8_t vision_min_confidence;
    uint8_t max_align_iterations;
    int8_t vision_x_sign;
    int8_t vision_y_sign;
    float align_nudge_mm;
    float align_speed_sps;
    uint8_t tray_speed_angle;
    uint32_t tray_index_ms;
    /* 盲走距离 mm（正=前进，负=后退）：横线 → 第一个物料/靶环、物料之间的间距、
       抓完 3 个后回到黑线的距离。必须按实车标定。 */
    int32_t source_enter_mm;
    int32_t source_step_mm;
    int32_t source_exit_mm;
    int32_t target_enter_mm;
    int32_t target_step_mm;
    station_detector_config_t station;
} competition_config_t;

typedef struct {
    competition_state_t state;
    competition_error_t error;
    bool running;
    bool debug_paused;      /* 站点暂停中，允许 Web 手动接管 */
    bool resume_crossing;   /* 恢复后正在定步跨出横线 */
    uint8_t pause_kind;     /* 0 无 / 1 物料站 / 2 靶环站 */
    bool config_confirmed;
    bool z_zero_valid;
    uint8_t picked_count;
    uint8_t placed_count;
    uint8_t align_iterations;
    vision_color_t current_color;
    vision_color_t required_color;
    int32_t z_position;
    int64_t state_elapsed_ms;
    int64_t run_elapsed_ms;
    tray_status_t tray;
    k230_link_status_t link;
    vision_result_t vision;
} competition_status_t;

esp_err_t competition_init(void);
esp_err_t competition_start(void);
void competition_abort(competition_error_t error);

/**
 * @brief 是否处于“逢横线即停”的人工抓取暂停
 *
 * 只读 volatile 标志，不获取 s_lock；Web 手动命令可直接调用，不会被状态机阻塞。
 */
bool competition_debug_paused(void);

/** 请求恢复循迹：底盘先定步前移跨出横线，再重置站点检测并回到原循迹状态。 */
esp_err_t competition_debug_resume(void);

/**
 * @brief 执行本站动作（人工将车停到目标位置后调用）
 *
 * 物料站 → 启动“地面抓取 → 装盘”脚本；靶环站 → 启动“盘内取出 → 放置”脚本。
 * 仅在 DEBUG_PAUSE、执行器静止且物料盘空闲时成功。全程不依赖 K230 视觉。
 */
esp_err_t competition_debug_execute(void);
bool competition_auto_active(void);
void competition_get_status(competition_status_t *out);
void competition_get_config(competition_config_t *out);
esp_err_t competition_set_config(const competition_config_t *config, bool confirm);
esp_err_t competition_set_z_zero(void);
esp_err_t competition_confirm_config(void);
const char *competition_state_name(competition_state_t state);
const char *competition_error_name(competition_error_t error);

#ifdef __cplusplus
}
#endif
