/**
 * @file wifi_sta.h
 * @brief Wi-Fi STA 自动连接（关闭省电模式，保证 Web 控制低延迟）
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_sta_init(void);
bool wifi_sta_connected(void);
const char *wifi_sta_ip(void);

#ifdef __cplusplus
}
#endif
