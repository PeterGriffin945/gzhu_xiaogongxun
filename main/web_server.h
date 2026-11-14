/**
 * @file web_server.h
 * @brief 异步 HTTP 调试控制台 (esp_http_server)
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_server_start(void);

#ifdef __cplusplus
}
#endif
