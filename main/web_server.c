#include "web_server.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "ads1115.h"
#include "app_config.h"
#include "chassis.h"
#include "competition.h"
#include "line_follow.h"
#include "lu9685.h"
#include "mecanum.h"
#include "safety.h"
#include "settings.h"
#include "stepper.h"
#include "web_page.h"
#include "wifi_sta.h"

static const char *TAG = "http";

static bool query_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen >= 1024) {
        return false;
    }
    char query[1024];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

/* 严格解析：非法/非有限数值视为“参数无效”，避免 NaN/Inf 污染控制状态，
   也避免 atoi("abc")==0 被当成合法指令执行。 */
static bool query_float(httpd_req_t *req, const char *key, float *out)
{
    char val[32];
    if (!query_value(req, key, val, sizeof(val))) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    float value = strtof(val, &end);
    if (end == val || *end != '\0' || errno == ERANGE || !isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

static bool query_int(httpd_req_t *req, const char *key, int *out)
{
    char val[32];
    if (!query_value(req, key, val, sizeof(val))) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long value = strtol(val, &end, 10);
    if (end == val || *end != '\0' || errno == ERANGE ||
        value < INT_MIN || value > INT_MAX) {
        return false;
    }
    *out = (int)value;
    return true;
}

static esp_err_t send_ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"ok\":1}");
}

static esp_err_t send_err(httpd_req_t *req, const char *msg)
{
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, msg);
}

/* 急停锁存期间所有运动类命令一律拒绝，必须显式解除 */
static bool safety_blocked(httpd_req_t *req)
{
    if (!safety_is_stopped()) {
        return false;
    }
    httpd_resp_set_status(req, "423 Locked");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":0,\"msg\":\"emergency stop active\"}");
    return true;
}

static bool manual_allowed(httpd_req_t *req)
{
    if (safety_blocked(req)) {
        return false;
    }
    if (!competition_auto_active()) {
        return true;
    }
    httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":0,\"msg\":\"automatic mission owns actuators\"}");
    return false;
}

/* 调试暂停期间允许人工接管底盘/升降/舵机；其余自动流程仍独占执行器 */
static bool manual_allowed_or_debug(httpd_req_t *req)
{
    if (safety_blocked(req)) {
        return false;
    }
    if (!competition_auto_active() || competition_debug_paused()) {
        return true;
    }
    httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":0,\"msg\":\"automatic mission owns actuators\"}");
    return false;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=60");
    return httpd_resp_send(req, WEB_INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    chassis_status_t st = { 0 };
    line_params_t p;
    float v[LINE_SENSOR_COUNT] = { 0 };
    uint32_t seq = 0;
    int black[LINE_SENSOR_COUNT] = { 0 };
    chassis_get_status(&st);
    line_follow_get_params(&p);
    ads1115_get_latest(v, &seq);
    for (int i = 0; i < LINE_SENSOR_COUNT; ++i) {
        black[i] = line_follow_is_black(v[i]) ? 1 : 0;
    }
    float chassis_sps = 0.0f, lift_sps = 0.0f;
    chassis_get_speeds(&chassis_sps, &lift_sps);

    char buf[1600];
    int n = snprintf(buf, sizeof(buf),
        "{\"mode\":%d,\"vx\":%.2f,\"vy\":%.2f,\"w\":%.2f,"
        "\"en\":[%d,%d],\"th\":%.2f,\"inv\":%d,\"rev\":%d,"
        "\"kp\":%.2f,\"ki\":%.2f,\"kd\":%.2f,\"base\":%.2f,\"om\":%.1f,\"acc\":%.0f,"
        "\"cs\":%.0f,\"lsp\":%.0f,"
        "\"v\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f],"
        "\"b\":[%d,%d,%d,%d,%d,%d,%d,%d],"
        "\"srv\":[%.0f,%d,%d],\"srv_ok\":%d,\"proto\":%d,\"ctr\":[%d,%d,%d],"
        "\"lift\":%.0f,\"err\":%.2f,\"ls\":%d,\"zpos\":%" PRId32 ",\"zmove\":%d,"
        "\"ws\":[%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 "],"
        "\"seq\":%" PRIu32 ",\"estop\":%d,\"wifi\":%d,\"ip\":\"%s\"}",
        (int)st.mode, st.vx, st.vy, st.omega,
        stepper_chassis_enabled() ? 1 : 0, stepper_lift_enabled() ? 1 : 0,
        p.threshold, p.black_high ? 1 : 0, p.reverse ? 1 : 0,
        p.kp, p.ki, p.kd, p.base_speed, p.max_omega, stepper_get_accel(),
        chassis_sps, lift_sps,
        v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7],
        black[0], black[1], black[2], black[3], black[4], black[5], black[6], black[7],
        (double)lu9685_get_pos_deg(LU9685_CH_BASE), lu9685_get_angle(LU9685_CH_TRAY),
        lu9685_get_angle(LU9685_CH_GRIPPER), lu9685_available() ? 1 : 0,
        (int)lu9685_get_protocol(), lu9685_get_center(LU9685_CH_BASE),
        lu9685_get_center(LU9685_CH_TRAY), lu9685_get_center(LU9685_CH_GRIPPER),
        st.lift_sps, st.line_error, st.line_seen ? 1 : 0,
        stepper_get_position(AXIS_LIFT), stepper_move_active(AXIS_LIFT) ? 1 : 0,
        st.wheel_steps[0], st.wheel_steps[1], st.wheel_steps[2], st.wheel_steps[3],
        seq, safety_is_stopped() ? 1 : 0,
        wifi_sta_connected() ? 1 : 0, wifi_sta_ip());
    if (n < 0 || n >= (int)sizeof(buf)) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t move_handler(httpd_req_t *req)
{
    if (!manual_allowed_or_debug(req)) {
        return ESP_OK;
    }
    float vx = 0.0f, vy = 0.0f, w = 0.0f;
    query_float(req, "vx", &vx);
    query_float(req, "vy", &vy);
    query_float(req, "w", &w);
    /* 自动流程（含调试暂停）期间不再由 /api/move 切模式：
       暂停时底盘已是 MANUAL，跨线时处于 POSITION，切模式会取消定步。
       非比赛状态下仍保持“一给速度就回手动”的旧行为。 */
    if (!competition_auto_active() && (vx != 0.0f || vy != 0.0f || w != 0.0f) &&
        chassis_get_mode() != CHASSIS_MODE_MANUAL) {
        chassis_set_mode(CHASSIS_MODE_MANUAL);
    }
    chassis_set_velocity(vx, vy, w);
    return send_ok(req);
}

static esp_err_t mode_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    int m = 0;
    if (!query_int(req, "m", &m) || (m != 0 && m != 1)) {
        return send_err(req, "{\"ok\":0,\"msg\":\"m=0|1 required\"}");
    }
    chassis_set_mode(m ? CHASSIS_MODE_LINE : CHASSIS_MODE_MANUAL);
    return send_ok(req);
}

static esp_err_t enable_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    int ch = -1, lf = -1;
    bool has_ch = query_int(req, "chassis", &ch);
    bool has_lf = query_int(req, "lift", &lf);
    if (!has_ch && !has_lf) {
        return send_err(req, "{\"ok\":0,\"msg\":\"chassis/lift required\"}");
    }
    if (has_ch) {
        stepper_set_chassis_enable(ch != 0);
    }
    if (has_lf) {
        stepper_set_lift_enable(lf != 0);
    }
    return send_ok(req);
}

static esp_err_t lift_handler(httpd_req_t *req)
{
    if (!manual_allowed_or_debug(req)) {
        return ESP_OK;
    }
    float v = 0.0f;
    if (!query_float(req, "v", &v)) {
        return send_err(req, "{\"ok\":0,\"msg\":\"v required\"}");
    }
    chassis_lift_jog(v);
    return send_ok(req);
}

static esp_err_t servo_handler(httpd_req_t *req)
{
    if (!manual_allowed_or_debug(req)) {
        return ESP_OK;
    }
    int ch = 0, a = 90;
    if (!query_int(req, "ch", &ch) || !query_int(req, "a", &a) || ch < 0 || ch >= 16) {
        return send_err(req, "{\"ok\":0,\"msg\":\"valid ch & a required\"}");
    }
    if (!lu9685_available()) {
        return send_err(req, "{\"ok\":0,\"msg\":\"LU9685 offline\"}");
    }
    esp_err_t err;
    if (ch == LU9685_CH_BASE) {
        /* CH0 底座：连续舵机定时点动。a 为逻辑目标角 0~360，
           时长 = |a - 当前| × ms_per_180 / 180，到点自动回中位停止。 */
        if (a < 0) {
            a = 0;
        } else if (a > 360) {
            a = 360;
        }
        competition_config_t cfg = { 0 };
        competition_get_config(&cfg);
        uint8_t speed = cfg.manipulator.base_speed_angle;
        uint32_t sweep = cfg.manipulator.base_sweep_ms;
        if (speed < 10 || speed > 170) {
            speed = ARM_BASE_SPEED_ANGLE_DEFAULT;
        }
        if (sweep < 200 || sweep > 60000U) {
            sweep = ARM_BASE_SWEEP_MS_DEFAULT;
        }
        err = lu9685_move_timed((uint8_t)ch, (uint16_t)a, speed, sweep, -1, NULL);
    } else if (lu9685_get_mode((uint8_t)ch) == LU9685_MODE_CONTINUOUS) {
        /* 连续舵机不能直接写角度，否则会一直转；请用 /servo_pulse 或盘位命令 */
        return send_err(req, "{\"ok\":0,\"msg\":\"continuous servo: use pulse/tray commands\"}");
    } else {
        if (a < 0) {
            a = 0;
        } else if (a > 180) {
            a = 180;
        }
        err = lu9685_set_angle((uint8_t)ch, (uint8_t)a);
    }
    return (err == ESP_OK) ? send_ok(req) : send_err(req, "{\"ok\":0,\"msg\":\"servo write failed or out of limits\"}");
}

static esp_err_t servo_proto_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    int mode = 0;
    if (!query_int(req, "mode", &mode) || mode < 0 || mode > 2) {
        return send_err(req, "{\"ok\":0,\"msg\":\"mode=0|1|2\"}");
    }
    static const uint8_t chs[3] = { LU9685_CH_BASE, LU9685_CH_TRAY, LU9685_CH_GRIPPER };
    /* 正在点动的通道先按旧协议停止，避免切协议后失去自动停止 */
    for (int i = 0; i < 3; ++i) {
        if (lu9685_get_mode(chs[i]) == LU9685_MODE_CONTINUOUS &&
            lu9685_pulse_active(chs[i])) {
            lu9685_pulse_cancel(chs[i]);
        }
    }
    lu9685_set_protocol((uint8_t)mode);
    /* 按新协议重发当前姿态：位置舵机保持当前角度，连续舵机回中位停转 */
    for (int i = 0; i < 3; ++i) {
        if (lu9685_get_mode(chs[i]) == LU9685_MODE_POSITION) {
            lu9685_move_deg(chs[i], lu9685_get_pos_deg(chs[i]));
        } else {
            lu9685_set_angle(chs[i], lu9685_get_center(chs[i]));
        }
    }
    settings_request_save();
    return send_ok(req);
}

static esp_err_t servo_pulse_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    int ch = -1, angle = 90, dur_ms = 0, stop = -1;
    if (!query_int(req, "ch", &ch) || !query_int(req, "speed_angle", &angle) ||
        !query_int(req, "duration_ms", &dur_ms) || ch < 0 || ch >= 16 || dur_ms < 0) {
        return send_err(req, "{\"ok\":0,\"msg\":\"invalid pulse arguments\"}");
    }
    query_int(req, "stop", &stop);
    if (angle < 0) {
        angle = 0;
    } else if (angle > 180) {
        angle = 180;
    }
    if (stop > 180) {
        stop = 180;
    }
    esp_err_t err = lu9685_pulse_start((uint8_t)ch, (uint8_t)angle, (uint32_t)dur_ms, stop);
    return (err == ESP_OK) ? send_ok(req) : send_err(req, "{\"ok\":0,\"msg\":\"pulse unavailable\"}");
}

static esp_err_t servo_stop_handler(httpd_req_t *req)
{
    if (!manual_allowed_or_debug(req)) {
        return ESP_OK;
    }
    int ch = -1;
    if (!query_int(req, "ch", &ch) || ch < 0 || ch >= 16) {
        return send_err(req, "{\"ok\":0,\"msg\":\"ch required\"}");
    }
    /* 连续舵机：取消定时并写回中位（停转）；位置舵机：无动作，返回 OK */
    esp_err_t err = lu9685_pulse_cancel((uint8_t)ch);
    return (err == ESP_OK) ? send_ok(req) : send_err(req, "{\"ok\":0,\"msg\":\"servo stop failed\"}");
}

static esp_err_t servo_center_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    int ch = -1, a = 90;
    if (!query_int(req, "ch", &ch) || !query_int(req, "a", &a) || ch < 0 || ch >= 16) {
        return send_err(req, "{\"ok\":0,\"msg\":\"ch & a required\"}");
    }
    if (a < 0) {
        a = 0;
    } else if (a > 180) {
        a = 180;
    }
    lu9685_set_center((uint8_t)ch, (uint8_t)a);
    settings_request_save();
    return send_ok(req);
}

static esp_err_t speed_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    float cs = 0.0f, ls = 0.0f, value = 0.0f;
    chassis_get_speeds(&cs, &ls);
    bool has_cs = query_float(req, "chassis", &value);
    if (has_cs) {
        cs = value;
    }
    bool has_ls = query_float(req, "lift", &value);
    if (has_ls) {
        ls = value;
    }
    if (!has_cs && !has_ls) {
        return send_err(req, "{\"ok\":0,\"msg\":\"chassis/lift required\"}");
    }
    chassis_set_speeds(cs, ls);
    settings_request_save();
    return send_ok(req);
}

static esp_err_t thresh_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    float th = 0.0f;
    if (!query_float(req, "th", &th) && !query_float(req, "v", &th)) {
        return send_err(req, "{\"ok\":0,\"msg\":\"th required\"}");
    }
    line_params_t p;
    line_follow_get_params(&p);
    p.threshold = th;
    line_follow_set_params(&p);
    settings_request_save();
    return send_ok(req);
}

static esp_err_t line_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    line_params_t p;
    line_follow_get_params(&p);
    float f;
    int i;
    if (query_float(req, "th", &f)) p.threshold = f;
    if (query_int(req, "inv", &i)) p.black_high = (i != 0);
    if (query_int(req, "rev", &i)) p.reverse = (i != 0);
    if (query_float(req, "kp", &f)) p.kp = f;
    if (query_float(req, "ki", &f)) p.ki = f;
    if (query_float(req, "kd", &f)) p.kd = f;
    if (query_float(req, "base", &f)) {
        p.base_speed = f;
        float cs = 0.0f, ls = 0.0f;
        chassis_get_speeds(&cs, &ls);
        chassis_set_speeds((float)mecanum_mps_to_steps(f), ls);
    }
    if (query_float(req, "om", &f)) p.max_omega = f;
    if (query_float(req, "accel", &f)) stepper_set_accel(f);
    line_follow_set_params(&p);
    settings_request_save();
    return send_ok(req);
}

static esp_err_t stop_handler(httpd_req_t *req)
{
    /* 平滑停车 + 中止比赛（不锁存，可立即继续手动操作） */
    lu9685_pulse_stop_all();
    competition_abort(COMPETITION_ERROR_MANUAL_STOP);
    return send_ok(req);
}

static esp_err_t estop_handler(httpd_req_t *req)
{
    safety_emergency_stop();
    return send_ok(req);
}

static esp_err_t estop_clear_handler(httpd_req_t *req)
{
    safety_clear();
    return send_ok(req);
}

static esp_err_t settings_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    int save = 0;
    if (!query_int(req, "save", &save) || save == 0) {
        return send_err(req, "{\"ok\":0,\"msg\":\"save=1 required\"}");
    }
    esp_err_t err = settings_save_now();
    return (err == ESP_OK) ? send_ok(req)
                           : send_err(req, "{\"ok\":0,\"msg\":\"save failed\"}");
}

static esp_err_t competition_status_handler(httpd_req_t *req)
{
    competition_status_t st = { 0 };
    competition_config_t cfg = { 0 };
    competition_get_status(&st);
    competition_get_config(&cfg);

    /* 物料/靶环相对画面中心的偏差 → 小车该往哪边移动多少（文本提示用）。
       像素→毫米比例见 app_config.h 的 VISION_MM_PER_PX。 */
    int target_cx = (st.vision.mode == VISION_MODE_RING) ? cfg.ring_target_cx
                                                         : cfg.material_target_cx;
    int target_cy = (st.vision.mode == VISION_MODE_RING) ? cfg.ring_target_cy
                                                         : cfg.material_target_cy;
    int aim_dx = 0, aim_dy = 0, aim_aligned = 0;
    float aim_mmx = 0.0f, aim_mmy = 0.0f, aim_dist = 0.0f;
    if (st.vision.detected) {
        aim_dx = (int)st.vision.cx - target_cx;
        aim_dy = (int)st.vision.cy - target_cy;
        aim_aligned = (abs(aim_dx) <= cfg.vision_tolerance_px &&
                       abs(aim_dy) <= cfg.vision_tolerance_px) ? 1 : 0;
        aim_mmx = (float)aim_dx * VISION_MM_PER_PX * (float)cfg.vision_x_sign;
        aim_mmy = (float)aim_dy * VISION_MM_PER_PX * (float)cfg.vision_y_sign;
        aim_dist = sqrtf(aim_mmx * aim_mmx + aim_mmy * aim_mmy);
    }

    char buf[4096];
    int n = snprintf(buf, sizeof(buf),
        "{\"ok\":1,\"state\":\"%s\",\"error\":\"%s\",\"running\":%d,"
        "\"dbg\":%d,\"cross\":%d,\"pk\":%d,"
        "\"cfgok\":%d,\"zok\":%d,\"picked\":%d,\"placed\":%d,\"align\":%d,"
        "\"color\":%d,\"required\":%d,\"zpos\":%" PRId32 ",\"state_ms\":%" PRId64 ",\"run_ms\":%" PRId64 ","
        "\"tray\":{\"zero\":%d,\"slot\":%d,\"target\":%d,\"motion\":%d,\"speed\":%d,\"ms\":%" PRIu32 ",\"colors\":[%d,%d,%d]},"
        "\"link\":{\"online\":%d,\"mode\":%d,\"ack\":%d,\"frames\":%" PRIu32 "},"
        "\"vision\":{\"mode\":%d,\"color\":%d,\"detected\":%d,\"stable\":%d,\"cx\":%d,\"cy\":%d,\"area\":%d,\"conf\":%d,\"age\":%" PRId64 "},"
        "\"aim\":{\"dx\":%d,\"dy\":%d,\"mmx\":%.1f,\"mmy\":%.1f,\"dist\":%.1f,\"aligned\":%d,\"tol\":%d},"
        "\"cfg\":{\"zpick\":%" PRId32 ",\"ztray\":%" PRId32 ",\"zplace\":%" PRId32 ",\"zmax\":%" PRId32 ","
        "\"bstow\":%d,\"bpick\":%d,\"btray\":%d,\"bplace\":%d,\"gopen\":%d,\"gclose\":%d,"
        "\"gopenms\":%d,\"gclosems\":%d,"
        "\"bms\":%d,\"gms\":%d,\"bspeed\":%d,\"bsweep\":%d,\"lsps\":%.1f,\"mcx\":%d,\"mcy\":%d,\"rcx\":%d,\"rcy\":%d,"
        "\"tol\":%d,\"minconf\":%d,\"maxiter\":%d,\"xsign\":%d,\"ysign\":%d,\"nudge\":%.2f,\"asps\":%.1f,"
        "\"tspeed\":%d,\"tms\":%" PRIu32 ",\"sblack\":%d,\"sconfirm\":%d,\"sclear\":%d,\"srearm\":%d,\"sinterval\":%" PRIu32 "}}",
        competition_state_name(st.state), competition_error_name(st.error), st.running ? 1 : 0,
        st.debug_paused ? 1 : 0, st.resume_crossing ? 1 : 0, st.pause_kind,
        st.config_confirmed ? 1 : 0, st.z_zero_valid ? 1 : 0, st.picked_count, st.placed_count,
        st.align_iterations, st.current_color, st.required_color, st.z_position,
        st.state_elapsed_ms, st.run_elapsed_ms,
        st.tray.zero_valid ? 1 : 0, st.tray.current_slot, st.tray.target_slot,
        st.tray.motion, st.tray.speed_angle, st.tray.index_duration_ms,
        st.tray.colors[0], st.tray.colors[1], st.tray.colors[2],
        k230_link_online() ? 1 : 0,
        st.link.requested_mode, st.link.ack_mode, st.link.valid_frames,
        st.vision.mode, st.vision.color, st.vision.detected ? 1 : 0, st.vision.stable ? 1 : 0,
        st.vision.cx, st.vision.cy, st.vision.area, st.vision.confidence,
        st.vision.received_ms > 0 ? esp_timer_get_time() / 1000 - st.vision.received_ms : -1,
        aim_dx, aim_dy, aim_mmx, aim_mmy, aim_dist, aim_aligned, cfg.vision_tolerance_px,
        cfg.manipulator.z_ground_pick, cfg.manipulator.z_tray, cfg.manipulator.z_ground_place,
        cfg.manipulator.z_max, cfg.manipulator.base_stow, cfg.manipulator.base_pick,
        cfg.manipulator.base_tray, cfg.manipulator.base_place, cfg.manipulator.gripper_open,
        cfg.manipulator.gripper_closed, cfg.manipulator.gripper_open_ms,
        cfg.manipulator.gripper_close_ms, cfg.manipulator.base_settle_ms,
        cfg.manipulator.gripper_settle_ms, cfg.manipulator.base_speed_angle,
        cfg.manipulator.base_sweep_ms, cfg.manipulator.lift_speed_sps,
        cfg.material_target_cx, cfg.material_target_cy, cfg.ring_target_cx, cfg.ring_target_cy,
        cfg.vision_tolerance_px, cfg.vision_min_confidence, cfg.max_align_iterations,
        cfg.vision_x_sign, cfg.vision_y_sign, cfg.align_nudge_mm, cfg.align_speed_sps,
        cfg.tray_speed_angle, cfg.tray_index_ms, cfg.station.min_black,
        cfg.station.confirm_frames, cfg.station.clear_max_black, cfg.station.rearm_frames,
        cfg.station.min_interval_ms);
    if (n < 0 || n >= (int)sizeof(buf)) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t competition_command_handler(httpd_req_t *req)
{
    char command[24];
    if (!query_value(req, "cmd", command, sizeof(command)) &&
        !query_value(req, "action", command, sizeof(command))) {
        return send_err(req, "{\"ok\":0,\"msg\":\"cmd required\"}");
    }
    esp_err_t err = ESP_OK;
    if (strcmp(command, "start") == 0) {
        err = competition_start();
    } else if (strcmp(command, "abort") == 0) {
        competition_abort(COMPETITION_ERROR_MANUAL_STOP);
    } else if (strcmp(command, "z_zero") == 0) {
        err = competition_set_z_zero();
    } else if (strcmp(command, "confirm") == 0) {
        err = competition_confirm_config();
        if (err == ESP_OK) {
            competition_config_t cfg = { 0 };
            competition_get_config(&cfg);
            settings_save_competition(&cfg, true);
        }
    } else if (strcmp(command, "resume") == 0 || strcmp(command, "skip") == 0) {
        err = competition_debug_resume();
    } else if (strcmp(command, "execute") == 0) {
        err = competition_debug_execute();
        if (err == ESP_ERR_INVALID_STATE) {
            return send_err(req, "{\"ok\":0,\"msg\":\"请先停止底盘/升降/物料盘再执行\"}");
        }
    } else {
        /* 调试暂停时也允许人工转盘选槽，方便靶环站放置取料 */
        if (!manual_allowed_or_debug(req)) {
            return ESP_OK;
        }
        if (strcmp(command, "tray_zero") == 0) {
            err = tray_controller_set_zero();
        } else if (strcmp(command, "tray_next") == 0) {
            err = tray_controller_start_next();
        } else if (strcmp(command, "tray_to") == 0) {
            int slot = -1;
            if (!query_int(req, "slot", &slot) || slot < 0 || slot >= TRAY_SLOT_COUNT) {
                return send_err(req, "{\"ok\":0,\"msg\":\"slot=0..5 required\"}");
            }
            err = tray_controller_start_to((uint8_t)slot);
        } else {
            return send_err(req, "{\"ok\":0,\"msg\":\"unknown command\"}");
        }
    }
    if (err != ESP_OK) {
        return send_err(req, "{\"ok\":0,\"msg\":\"precondition or device error\"}");
    }
    return send_ok(req);
}

#define READ_CFG_INT(key, field) do { int value; if (query_int(req, key, &value)) { field = value; cfg_changed = true; } } while (0)
#define READ_CFG_FLOAT(key, field) do { float value; if (query_float(req, key, &value)) { field = value; cfg_changed = true; } } while (0)

static esp_err_t competition_config_handler(httpd_req_t *req)
{
    if (!manual_allowed(req)) {
        return ESP_OK;
    }
    competition_config_t cfg = { 0 };
    bool cfg_changed = false;
    competition_get_config(&cfg);
    READ_CFG_INT("zpick", cfg.manipulator.z_ground_pick);
    READ_CFG_INT("ztray", cfg.manipulator.z_tray);
    READ_CFG_INT("zplace", cfg.manipulator.z_ground_place);
    READ_CFG_INT("zmax", cfg.manipulator.z_max);
    READ_CFG_INT("bstow", cfg.manipulator.base_stow);
    READ_CFG_INT("bpick", cfg.manipulator.base_pick);
    READ_CFG_INT("btray", cfg.manipulator.base_tray);
    READ_CFG_INT("bplace", cfg.manipulator.base_place);
    READ_CFG_INT("gopen", cfg.manipulator.gripper_open);
    READ_CFG_INT("gclose", cfg.manipulator.gripper_closed);
    READ_CFG_INT("gopenms", cfg.manipulator.gripper_open_ms);
    READ_CFG_INT("gclosems", cfg.manipulator.gripper_close_ms);
    READ_CFG_INT("bms", cfg.manipulator.base_settle_ms);
    READ_CFG_INT("gms", cfg.manipulator.gripper_settle_ms);
    READ_CFG_INT("bspeed", cfg.manipulator.base_speed_angle);
    READ_CFG_INT("bsweep", cfg.manipulator.base_sweep_ms);
    READ_CFG_FLOAT("lsps", cfg.manipulator.lift_speed_sps);
    READ_CFG_INT("mcx", cfg.material_target_cx);
    READ_CFG_INT("mcy", cfg.material_target_cy);
    READ_CFG_INT("rcx", cfg.ring_target_cx);
    READ_CFG_INT("rcy", cfg.ring_target_cy);
    READ_CFG_INT("tol", cfg.vision_tolerance_px);
    READ_CFG_INT("minconf", cfg.vision_min_confidence);
    READ_CFG_INT("maxiter", cfg.max_align_iterations);
    READ_CFG_INT("xsign", cfg.vision_x_sign);
    READ_CFG_INT("ysign", cfg.vision_y_sign);
    READ_CFG_FLOAT("nudge", cfg.align_nudge_mm);
    READ_CFG_FLOAT("asps", cfg.align_speed_sps);
    READ_CFG_INT("tspeed", cfg.tray_speed_angle);
    READ_CFG_INT("tms", cfg.tray_index_ms);
    READ_CFG_INT("sblack", cfg.station.min_black);
    READ_CFG_INT("sconfirm", cfg.station.confirm_frames);
    READ_CFG_INT("sclear", cfg.station.clear_max_black);
    READ_CFG_INT("srearm", cfg.station.rearm_frames);
    READ_CFG_INT("sinterval", cfg.station.min_interval_ms);
    int confirm = 0;
    bool has_confirm = query_int(req, "confirm", &confirm);
    if (!cfg_changed && !has_confirm) {
        /* 没有任何已知字段（多半是键名拼错）时明确报错，而不是静默重确认旧值。 */
        return send_err(req, "{\"ok\":0,\"msg\":\"no recognized config fields\"}");
    }
    esp_err_t err = competition_set_config(&cfg, confirm != 0);
    if (err == ESP_OK) {
        /* 标定值持久化：下次上电直接恢复，动作脚本无需重填 */
        settings_save_competition(&cfg, confirm != 0);
    }
    return (err == ESP_OK) ? send_ok(req) : send_err(req, "{\"ok\":0,\"msg\":\"invalid calibration values\"}");
}

/* 示教：记录当前底座/夹爪/升降姿态到对应动作步骤，并立即持久化 */
static esp_err_t competition_teach_handler(httpd_req_t *req)
{
    if (!manual_allowed_or_debug(req)) {
        return ESP_OK;
    }
    char step[24];
    if (!query_value(req, "step", step, sizeof(step))) {
        return send_err(req, "{\"ok\":0,\"msg\":\"step required\"}");
    }

    competition_config_t cfg = { 0 };
    competition_get_config(&cfg);
    competition_status_t st = { 0 };
    competition_get_status(&st);

    long value = 0;
    bool known = true;
    if (strcmp(step, "bstow") == 0) {
        value = lroundf(lu9685_get_pos_deg(LU9685_CH_BASE));
        cfg.manipulator.base_stow = (uint16_t)value;
    } else if (strcmp(step, "bpick") == 0) {
        value = lroundf(lu9685_get_pos_deg(LU9685_CH_BASE));
        cfg.manipulator.base_pick = (uint16_t)value;
    } else if (strcmp(step, "btray") == 0) {
        value = lroundf(lu9685_get_pos_deg(LU9685_CH_BASE));
        cfg.manipulator.base_tray = (uint16_t)value;
    } else if (strcmp(step, "bplace") == 0) {
        value = lroundf(lu9685_get_pos_deg(LU9685_CH_BASE));
        cfg.manipulator.base_place = (uint16_t)value;
    } else if (strcmp(step, "gopen") == 0 || strcmp(step, "gclose") == 0) {
        /* 夹爪已是连续舵机：没有“当前角度”可示教，请在配置里直接填速度角与通电时间 */
        return send_err(req, "{\"ok\":0,\"msg\":\"gripper is continuous: set speed/time in config\"}");
    } else if (strcmp(step, "zpick") == 0) {
        value = stepper_get_position(AXIS_LIFT);
        if (value < 0) {
            value = 0;
        }
        cfg.manipulator.z_ground_pick = (int32_t)value;
    } else if (strcmp(step, "ztray") == 0) {
        value = stepper_get_position(AXIS_LIFT);
        if (value < 0) {
            value = 0;
        }
        cfg.manipulator.z_tray = (int32_t)value;
    } else if (strcmp(step, "zplace") == 0) {
        value = stepper_get_position(AXIS_LIFT);
        if (value < 0) {
            value = 0;
        }
        cfg.manipulator.z_ground_place = (int32_t)value;
    } else {
        known = false;
    }

    if (!known) {
        return send_err(req, "{\"ok\":0,\"msg\":\"unknown step\"}");
    }
    esp_err_t err = competition_set_config(&cfg, st.config_confirmed);
    if (err != ESP_OK) {
        return send_err(req, "{\"ok\":0,\"msg\":\"teach rejected (running/tray busy/invalid)\"}");
    }
    settings_save_competition(&cfg, st.config_confirmed);

    char buf[96];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":1,\"step\":\"%s\",\"value\":%ld}",
                     step, value);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* /api/competition 的 4KB JSON 缓冲区 + snprintf 参数全部在本任务栈上，
       8192 余量偏紧，提高到 12KB。 */
    config.stack_size = 12288;
    config.max_uri_handlers = 28;
    config.max_uri_len = 1024;
    config.lru_purge_enable = true;
    config.keep_alive_enable = true;
    config.server_port = 80;
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }
    static const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_handler },
        { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_handler },
        { .uri = "/api/move", .method = HTTP_GET, .handler = move_handler },
        { .uri = "/api/mode", .method = HTTP_GET, .handler = mode_handler },
        { .uri = "/api/en", .method = HTTP_GET, .handler = enable_handler },
        { .uri = "/api/lift", .method = HTTP_GET, .handler = lift_handler },
        { .uri = "/api/servo", .method = HTTP_GET, .handler = servo_handler },
        { .uri = "/api/line", .method = HTTP_GET, .handler = line_handler },
        { .uri = "/api/stop", .method = HTTP_GET, .handler = stop_handler },
        { .uri = "/api/estop", .method = HTTP_GET, .handler = estop_handler },
        { .uri = "/api/estop/clear", .method = HTTP_GET, .handler = estop_clear_handler },
        { .uri = "/thresh", .method = HTTP_GET, .handler = thresh_handler },
        { .uri = "/speed", .method = HTTP_GET, .handler = speed_handler },
        { .uri = "/api/speed", .method = HTTP_GET, .handler = speed_handler },
        { .uri = "/servo_proto", .method = HTTP_GET, .handler = servo_proto_handler },
        { .uri = "/servo_pulse", .method = HTTP_GET, .handler = servo_pulse_handler },
        { .uri = "/servo_center", .method = HTTP_GET, .handler = servo_center_handler },
        { .uri = "/servo_stop", .method = HTTP_GET, .handler = servo_stop_handler },
        { .uri = "/api/competition", .method = HTTP_GET, .handler = competition_status_handler },
        { .uri = "/api/competition/cmd", .method = HTTP_GET, .handler = competition_command_handler },
        { .uri = "/api/competition/config", .method = HTTP_GET, .handler = competition_config_handler },
        { .uri = "/api/competition/teach", .method = HTTP_GET, .handler = competition_teach_handler },
        { .uri = "/settings", .method = HTTP_GET, .handler = settings_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) {
        err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", uris[i].uri, esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "HTTP console ready: http://%s/", wifi_sta_ip());
    return ESP_OK;
}
