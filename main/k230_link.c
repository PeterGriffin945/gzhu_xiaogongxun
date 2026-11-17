#include "k230_link.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"

#define K230_RX_BUFFER_SIZE 2048
#define K230_TASK_PERIOD_MS 20

static const char *TAG = "k230_link";
static SemaphoreHandle_t s_tx_mutex;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static vision_parser_t s_parser;
static vision_result_t s_result;
static k230_link_status_t s_status;
static uint8_t s_tx_seq;
static bool s_driver_ready;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static esp_err_t send_frame(uint8_t type, const uint8_t *payload, uint8_t length,
                            uint8_t *seq_out)
{
    if (!s_driver_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t raw[VISION_MAX_FRAME];
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    /* 序号在发送锁内递增：心跳任务与比赛任务并发发送时不会撞号。 */
    uint8_t seq = s_tx_seq++;
    size_t raw_length = sizeof(raw);
    if (!vision_encode_frame(type, seq, payload, length, raw, &raw_length)) {
        xSemaphoreGive(s_tx_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    int written = uart_write_bytes(K230_UART_PORT, raw, raw_length);
    xSemaphoreGive(s_tx_mutex);
    if (written != (int)raw_length) {
        return ESP_FAIL;
    }
    if (type == VISION_MSG_COMMAND && length == 1) {
        taskENTER_CRITICAL(&s_state_mux);
        s_status.last_command_seq = seq;
        taskEXIT_CRITICAL(&s_state_mux);
    }
    if (seq_out != NULL) {
        *seq_out = seq;
    }
    return ESP_OK;
}

static void handle_detection(const vision_frame_t *frame, int64_t received_ms)
{
    if (frame->length != 11 || frame->payload[0] > VISION_MODE_TRAY ||
        frame->payload[1] > VISION_COLOR_BLUE) {
        return;
    }
    vision_result_t result = {
        .mode = (vision_mode_t)frame->payload[0],
        .color = (vision_color_t)frame->payload[1],
        .detected = (frame->payload[2] & 0x01) != 0,
        .stable = (frame->payload[2] & 0x02) != 0,
        .cx = (uint16_t)frame->payload[3] | ((uint16_t)frame->payload[4] << 8),
        .cy = (uint16_t)frame->payload[5] | ((uint16_t)frame->payload[6] << 8),
        .area = (uint16_t)frame->payload[7] | ((uint16_t)frame->payload[8] << 8),
        .confidence = frame->payload[9],
        .seq = frame->seq,
        .received_ms = received_ms,
    };
    if (!result.detected) {
        result.stable = false;
        result.color = VISION_COLOR_UNKNOWN;
    }
    taskENTER_CRITICAL(&s_state_mux);
    s_result = result;
    taskEXIT_CRITICAL(&s_state_mux);
}

static void handle_frame(const vision_frame_t *frame)
{
    int64_t received_ms = now_ms();
    taskENTER_CRITICAL(&s_state_mux);
    s_status.valid_frames++;
    s_status.last_rx_seq = frame->seq;
    s_status.last_rx_ms = received_ms;
    taskEXIT_CRITICAL(&s_state_mux);

    if (frame->type == VISION_MSG_DETECTION) {
        handle_detection(frame, received_ms);
    } else if (frame->type == VISION_MSG_ACK && frame->length == 2 &&
               frame->payload[1] <= VISION_MODE_TRAY) {
        taskENTER_CRITICAL(&s_state_mux);
        s_status.last_ack_command_seq = frame->payload[0];
        s_status.ack_mode = (vision_mode_t)frame->payload[1];
        taskEXIT_CRITICAL(&s_state_mux);
    }
}

static void k230_link_task(void *arg)
{
    (void)arg;
    uint8_t rx[128];
    int64_t last_heartbeat = 0;
    while (1) {
        int count = uart_read_bytes(K230_UART_PORT, rx, sizeof(rx), pdMS_TO_TICKS(K230_TASK_PERIOD_MS));
        for (int i = 0; i < count; ++i) {
            vision_frame_t frame;
            if (vision_parser_feed(&s_parser, rx[i], &frame)) {
                handle_frame(&frame);
            }
        }
        int64_t current = now_ms();
        if (current - last_heartbeat >= 500) {
            send_frame(VISION_MSG_HEARTBEAT, NULL, 0, NULL);
            last_heartbeat = current;
        }
    }
}

esp_err_t k230_link_init(void)
{
    s_tx_mutex = xSemaphoreCreateMutex();
    if (s_tx_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uart_config_t config = {
        .baud_rate = K230_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_param_config(K230_UART_PORT, &config);
    if (err == ESP_OK) {
        err = uart_set_pin(K230_UART_PORT, PIN_K230_TX, PIN_K230_RX,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = uart_driver_install(K230_UART_PORT, K230_RX_BUFFER_SIZE, 0, 0, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_driver_ready = true;
    s_status.driver_ready = true;
    s_status.requested_mode = VISION_MODE_IDLE;
    s_status.ack_mode = VISION_MODE_IDLE;
    vision_parser_init(&s_parser);
    BaseType_t ret = xTaskCreatePinnedToCore(k230_link_task, "k230_link", 4096, NULL, 4, NULL, 0);
    if (ret != pdPASS) {
        uart_driver_delete(K230_UART_PORT);
        s_driver_ready = false;
        s_status.driver_ready = false;
        return ESP_ERR_NO_MEM;
    }
    /* ESP32 单独复位后 K230 可能还停留在上一次的模式，主动同步到 IDLE。 */
    uint8_t idle = VISION_MODE_IDLE;
    (void)send_frame(VISION_MSG_COMMAND, &idle, 1, NULL);

    ESP_LOGI(TAG, "UART%d TX=%d RX=%d baud=%d", K230_UART_PORT,
             PIN_K230_TX, PIN_K230_RX, K230_UART_BAUD);
    return ESP_OK;
}

static bool mode_acked(uint8_t seq, vision_mode_t mode)
{
    k230_link_status_t status;
    k230_link_get_status(&status);
    return status.last_ack_command_seq == seq && status.ack_mode == mode;
}

esp_err_t k230_link_set_mode(vision_mode_t mode)
{
    if (mode > VISION_MODE_TRAY) {
        return ESP_ERR_INVALID_ARG;
    }
    /* 台架调试/无视觉模式：K230 不在线时直接返回，避免每个状态白等 240ms ACK */
    if (!k230_link_online()) {
        taskENTER_CRITICAL(&s_state_mux);
        s_status.requested_mode = mode;
        taskEXIT_CRITICAL(&s_state_mux);
        return ESP_OK;
    }
    uint8_t payload = (uint8_t)mode;

    /* 指令可能被步进电机 EMI 破坏；等待 ACK 并重发，避免识别阶段空等 12s。 */
    for (int attempt = 0; attempt < 2; ++attempt) {
        uint8_t seq = 0;
        esp_err_t err = send_frame(VISION_MSG_COMMAND, &payload, 1, &seq);
        if (err != ESP_OK) {
            return err;
        }
        taskENTER_CRITICAL(&s_state_mux);
        s_status.requested_mode = mode;
        taskEXIT_CRITICAL(&s_state_mux);

        for (int wait = 0; wait < 12; ++wait) {
            if (mode_acked(seq, mode)) {
                return ESP_OK;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    /* 两轮都没有 ACK：命令本身已发出，交给上层识别超时处理，不误报链路错误。 */
    ESP_LOGW(TAG, "mode %d not acked, continuing", (int)mode);
    return ESP_OK;
}

bool k230_link_online(void)
{
    int64_t last_rx;
    taskENTER_CRITICAL(&s_state_mux);
    last_rx = s_status.last_rx_ms;
    taskEXIT_CRITICAL(&s_state_mux);
    return s_driver_ready && last_rx > 0 &&
           now_ms() - last_rx <= K230_LINK_TIMEOUT_MS;
}

bool k230_link_result_fresh(vision_mode_t mode)
{
    taskENTER_CRITICAL(&s_state_mux);
    vision_result_t result = s_result;
    taskEXIT_CRITICAL(&s_state_mux);
    return result.received_ms > 0 && result.mode == mode &&
           now_ms() - result.received_ms <= K230_RESULT_TIMEOUT_MS;
}

void k230_link_get_result(vision_result_t *out)
{
    if (out == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_state_mux);
    *out = s_result;
    taskEXIT_CRITICAL(&s_state_mux);
}

void k230_link_get_status(k230_link_status_t *out)
{
    if (out == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_state_mux);
    *out = s_status;
    taskEXIT_CRITICAL(&s_state_mux);
}
