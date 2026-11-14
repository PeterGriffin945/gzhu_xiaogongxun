/**
 * @file web_server.c
 * @brief HTTP 调试控制台：状态轮询 + 运动/舵机/循迹参数下发
 */
#include "web_server.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "ads1115.h"
#include "app_config.h"
#include "chassis.h"
#include "line_follow.h"
#include "lu9685.h"
#include "stepper.h"
#include "web_page.h"
#include "wifi_sta.h"

static const char *TAG = "http";

/* ---------------- 查询串工具 ---------------- */

static bool query_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen >= 512) {
        return false;
    }
    char query[512];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

static bool query_float(httpd_req_t *req, const char *key, float *out)
{
    char val[32];
    if (!query_value(req, key, val, sizeof(val))) {
        return false;
    }
    *out = strtof(val, NULL);
    return true;
}

static bool query_int(httpd_req_t *req, const char *key, int *out)
{
    char val[32];
    if (!query_value(req, key, val, sizeof(val))) {
        return false;
    }
    *out = atoi(val);
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
    return httpd_resp_sendstr(req, msg);
}

/* ---------------- 页面 ---------------- */
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

/* ---------------- 状态 ---------------- */
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

    char buf[1200];
    int n = snprintf(buf, sizeof(buf),
        "{\"mode\":%d,\"vx\":%.2f,\"vy\":%.2f,\"w\":%.2f,"
        "\"en\":[%d,%d],\"th\":%.2f,\"inv\":%d,\"rev\":%d,"
        "\"kp\":%.2f,\"ki\":%.2f,\"kd\":%.2f,\"base\":%.2f,\"om\":%.1f,\"acc\":%.0f,"
        "\"v\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f],"
        "\"b\":[%d,%d,%d,%d,%d,%d,%d,%d],"
        "\"srv\":[%d,%d,%d],\"srv_ok\":%d,\"lift\":%.0f,\"err\":%.2f,\"ls\":%d,"
        "\"ws\":[%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 "],"
        "\"seq\":%" PRIu32 ",\"wifi\":%d,\"ip\":\"%s\"}",
        (int)st.mode, st.vx, st.vy, st.omega,
        stepper_chassis_enabled() ? 1 : 0,
        stepper_lift_enabled() ? 1 : 0,
        p.threshold, p.black_high ? 1 : 0, p.reverse ? 1 : 0,
        p.kp, p.ki, p.kd, p.base_speed, p.max_omega, stepper_get_accel(),
        v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7],
        black[0], black[1], black[2], black[3],
        black[4], black[5], black[6], black[7],
        lu9685_get_angle(LU9685_CH_BASE),
        lu9685_get_angle(LU9685_CH_TRAY),
        lu9685_get_angle(LU9685_CH_GRIPPER),
        lu9685_available() ? 1 : 0,
        st.lift_sps, st.line_error, st.line_seen ? 1 : 0,
        st.wheel_steps[0], st.wheel_steps[1], st.wheel_steps[2], st.wheel_steps[3],
        seq, wifi_sta_connected() ? 1 : 0, wifi_sta_ip());

    if (n < 0 || n >= (int)sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

/* ---------------- 运动 ---------------- */
static esp_err_t move_handler(httpd_req_t *req)
{
    float vx = 0.0f, vy = 0.0f, w = 0.0f;
    query_float(req, "vx", &vx);
    query_float(req, "vy", &vy);
    query_float(req, "w", &w);

    if (vx != 0.0f || vy != 0.0f || w != 0.0f) {
        if (chassis_get_mode() != CHASSIS_MODE_MANUAL) {
            chassis_set_mode(CHASSIS_MODE_MANUAL);
        }
    }
    chassis_set_velocity(vx, vy, w);
    return send_ok(req);
}

static esp_err_t mode_handler(httpd_req_t *req)
{
    int m = 0;
    if (!query_int(req, "m", &m)) {
        return send_err(req, "{\"ok\":0,\"msg\":\"m required\"}");
    }
    if (m != 0 && m != 1) {
        return send_err(req, "{\"ok\":0,\"msg\":\"m=0|1\"}");
    }
    chassis_set_mode(m ? CHASSIS_MODE_LINE : CHASSIS_MODE_MANUAL);
    return send_ok(req);
}

static esp_err_t enable_handler(httpd_req_t *req)
{
    int ch = -1, lf = -1;
    bool has_ch = query_int(req, "chassis", &ch);
    bool has_lf = query_int(req, "lift", &lf);
    if (!has_ch && !has_lf) {
        return send_err(req, "{\"ok\":0,\"msg\":\"chassis/lift required\"}");
    }
    if (has_ch) {
        stepper_set_chassis_enable(ch != 0);
        ESP_LOGI(TAG, "chassis EN %s", ch ? "LOCK" : "RELEASE");
    }
    if (has_lf) {
        stepper_set_lift_enable(lf != 0);
        ESP_LOGI(TAG, "lift EN %s", lf ? "LOCK" : "RELEASE");
    }
    return send_ok(req);
}

static esp_err_t lift_handler(httpd_req_t *req)
{
    float v = 0.0f;
    if (!query_float(req, "v", &v)) {
        return send_err(req, "{\"ok\":0,\"msg\":\"v required\"}");
    }
    chassis_lift_jog(v);
    return send_ok(req);
}

static esp_err_t servo_handler(httpd_req_t *req)
{
    int ch = 0, a = 90;
    if (!query_int(req, "ch", &ch) || !query_int(req, "a", &a)) {
        return send_err(req, "{\"ok\":0,\"msg\":\"ch & a required\"}");
    }
    if (ch < 0 || ch >= 16) {
        return send_err(req, "{\"ok\":0,\"msg\":\"bad ch\"}");
    }
    if (!lu9685_available()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":0,\"msg\":\"LU9685 offline\"}");
    }
    lu9685_set_angle((uint8_t)ch, (uint8_t)((a < 0) ? 0 : (a > 180 ? 180 : a)));
    return send_ok(req);
}

static esp_err_t line_handler(httpd_req_t *req)
{
    line_params_t p;
    line_follow_get_params(&p);

    float f;
    int i;
    if (query_float(req, "th", &f)) {
        p.threshold = f;
    }
    if (query_int(req, "inv", &i)) {
        p.black_high = (i != 0);
    }
    if (query_int(req, "rev", &i)) {
        p.reverse = (i != 0);
    }
    if (query_float(req, "kp", &f)) {
        p.kp = f;
    }
    if (query_float(req, "ki", &f)) {
        p.ki = f;
    }
    if (query_float(req, "kd", &f)) {
        p.kd = f;
    }
    if (query_float(req, "base", &f)) {
        p.base_speed = f;
    }
    if (query_float(req, "om", &f)) {
        p.max_omega = f;
    }
    if (query_float(req, "accel", &f)) {
        stepper_set_accel(f);
    }
    line_follow_set_params(&p);
    return send_ok(req);
}

static esp_err_t stop_handler(httpd_req_t *req)
{
    chassis_stop();
    return send_ok(req);
}

/* ---------------- 启动 ---------------- */
esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;
    config.keep_alive_enable = true;
    config.server_port = 80;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/",            .method = HTTP_GET, .handler = index_handler },
        { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler },
        { .uri = "/api/status",  .method = HTTP_GET, .handler = status_handler },
        { .uri = "/api/move",    .method = HTTP_GET, .handler = move_handler },
        { .uri = "/api/mode",    .method = HTTP_GET, .handler = mode_handler },
        { .uri = "/api/en",      .method = HTTP_GET, .handler = enable_handler },
        { .uri = "/api/lift",    .method = HTTP_GET, .handler = lift_handler },
        { .uri = "/api/servo",   .method = HTTP_GET, .handler = servo_handler },
        { .uri = "/api/line",    .method = HTTP_GET, .handler = line_handler },
        { .uri = "/api/stop",    .method = HTTP_GET, .handler = stop_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); ++i) {
        err = httpd_register_uri_handler(server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed: %s", uris[i].uri,
                     esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "HTTP console ready: http://%s/", wifi_sta_ip());
    return ESP_OK;
}
