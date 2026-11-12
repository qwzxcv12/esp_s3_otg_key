#include "pn532_driver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "driver/gpio.h"

static const char *TAG = "PN532_HSU";

#define PN532_PREAMBLE                      (0x00)
#define PN532_STARTCODE1                    (0x00)
#define PN532_STARTCODE2                    (0xFF)
#define PN532_POSTAMBLE                     (0x00)

#define PN532_HOSTTOPN532                   (0xD4)
#define PN532_PN532TOHOST                   (0xD5)

// PN532 Commands
#define PN532_CMD_GETFIRMWAREVERSION        (0x02)
#define PN532_CMD_SAMCONFIGURATION          (0x14)
#define PN532_CMD_RFCONFIGURATION          (0x32)
#define PN532_CMD_INLISTPASSIVETARGET       (0x4A)

// ACK Frame: 0x00 0x00 0xFF 0x00 0xFF 0x00
static const uint8_t PN532_ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

static pn532_card_cb_t s_card_callback = NULL;
static TaskHandle_t s_scanner_task_handle = NULL;
static bool s_is_connected = false;
static bool s_scanner_active = true;

// Mặc định: Chân SDA của PN532 là TXD -> nối GPIO 6 (RX), chân SCL của PN532 là RXD -> nối GPIO 5 (TX)
static int s_current_rx_pin = PIN_PN532_GPIO_B; // GPIO 6
static int s_current_tx_pin = PIN_PN532_GPIO_A; // GPIO 5

/**
 * @brief Đánh thức PN532 trong chế độ UART HSU.
 */
static void pn532_wakeup(void) {
    uint8_t wakeup_seq[] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uart_write_bytes(PN532_UART_PORT, (const char *)wakeup_seq, sizeof(wakeup_seq));
    vTaskDelay(pdMS_TO_TICKS(15));
    uart_flush_input(PN532_UART_PORT);
}

/**
 * @brief Đọc và kiểm tra gói tin ACK từ PN532.
 */
static esp_err_t pn532_read_ack(void) {
    uint8_t buf[16] = {0};
    int read_len = uart_read_bytes(PN532_UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(80));
    if (read_len < 6) {
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i <= read_len - 6; i++) {
        if (memcmp(&buf[i], PN532_ACK, 6) == 0) {
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_RESPONSE;
}

/**
 * @brief Đóng gói và gửi lệnh xuống PN532 qua UART HSU.
 */
static esp_err_t pn532_write_command(const uint8_t *cmd, uint8_t cmd_len) {
    uart_flush_input(PN532_UART_PORT);

    uint8_t packet[64];
    if (cmd_len + 8 > sizeof(packet)) return ESP_ERR_INVALID_SIZE;

    uint8_t len = cmd_len + 1; // +1 cho TFI (0xD4)
    packet[0] = PN532_PREAMBLE;
    packet[1] = PN532_STARTCODE1;
    packet[2] = PN532_STARTCODE2;
    packet[3] = len;
    packet[4] = (uint8_t)(~len + 1); // Length Checksum (LCS)
    packet[5] = PN532_HOSTTOPN532;

    uint8_t sum = PN532_HOSTTOPN532;
    for (int i = 0; i < cmd_len; i++) {
        packet[6 + i] = cmd[i];
        sum += cmd[i];
    }
    packet[6 + cmd_len] = (uint8_t)(~sum + 1); // Data Checksum (DCS)
    packet[7 + cmd_len] = PN532_POSTAMBLE;

    uart_write_bytes(PN532_UART_PORT, (const char *)packet, cmd_len + 8);

    return pn532_read_ack();
}

/**
 * @brief Đọc gói phản hồi từ PN532 qua UART HSU.
 */
static esp_err_t pn532_read_response(uint8_t expected_cmd, uint8_t *out_data, uint8_t *out_len, uint32_t timeout_ms) {
    uint8_t rx_buf[64] = {0};
    int read_len = uart_read_bytes(PN532_UART_PORT, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(timeout_ms));
    if (read_len < 6) {
        return ESP_ERR_TIMEOUT;
    }

    int start_idx = -1;
    for (int i = 0; i <= read_len - 6; i++) {
        if (rx_buf[i] == 0x00 && rx_buf[i + 1] == 0xFF) {
            start_idx = i;
            break;
        }
    }

    if (start_idx < 0 || start_idx + 6 > read_len) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t len = rx_buf[start_idx + 2];
    uint8_t lcs = rx_buf[start_idx + 3];
    if ((uint8_t)(len + lcs) != 0) {
        return ESP_ERR_INVALID_CRC;
    }

    uint8_t tfi = rx_buf[start_idx + 4];
    uint8_t resp_cmd = rx_buf[start_idx + 5];

    if (tfi != PN532_PN532TOHOST || resp_cmd != (expected_cmd + 1)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t data_len = len - 2;
    if (out_data && out_len) {
        if (data_len > *out_len) data_len = *out_len;
        memcpy(out_data, &rx_buf[start_idx + 6], data_len);
        *out_len = data_len;
    }

    return ESP_OK;
}

/**
 * @brief Đọc thông tin Firmware Version từ PN532.
 */
static esp_err_t pn532_get_firmware_version(uint32_t *version_out) {
    uint8_t cmd = PN532_CMD_GETFIRMWAREVERSION;
    esp_err_t ret = pn532_write_command(&cmd, 1);
    if (ret != ESP_OK) return ret;

    uint8_t resp[4] = {0};
    uint8_t resp_len = sizeof(resp);
    ret = pn532_read_response(PN532_CMD_GETFIRMWAREVERSION, resp, &resp_len, 100);
    if (ret == ESP_OK && resp_len >= 4) {
        if (version_out) {
            *version_out = ((uint32_t)resp[0] << 24) |
                           ((uint32_t)resp[1] << 16) |
                           ((uint32_t)resp[2] << 8)  |
                           (uint32_t)resp[3];
        }
        return ESP_OK;
    }
    return ESP_FAIL;
}

/**
 * @brief Cấu hình SAM để bật từ trường RF cho anten.
 */
static esp_err_t pn532_sam_config(void) {
    uint8_t cmd[4] = {PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01};
    esp_err_t ret = pn532_write_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;

    uint8_t resp[2] = {0};
    uint8_t resp_len = sizeof(resp);
    return pn532_read_response(PN532_CMD_SAMCONFIGURATION, resp, &resp_len, 100);
}

/**
 * @brief Cài đặt MaxRetries = 1 lần.
 */
static esp_err_t pn532_set_passive_activation_retries(uint8_t max_retries) {
    uint8_t cmd[5] = {PN532_CMD_RFCONFIGURATION, 0x05, 0xFF, 0x01, max_retries};
    esp_err_t ret = pn532_write_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;

    uint8_t resp[2] = {0};
    uint8_t resp_len = sizeof(resp);
    return pn532_read_response(PN532_CMD_RFCONFIGURATION, resp, &resp_len, 100);
}

/**
 * @brief Quét thẻ Mifare / NTAG Type A qua UART HSU.
 */
static bool pn532_read_passive_target_id(uint8_t *uid, uint8_t *uid_len) {
    uint8_t cmd[3] = {PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00};
    if (pn532_write_command(cmd, sizeof(cmd)) != ESP_OK) {
        return false;
    }

    uint8_t resp[24] = {0};
    uint8_t resp_len = sizeof(resp);
    if (pn532_read_response(PN532_CMD_INLISTPASSIVETARGET, resp, &resp_len, 80) != ESP_OK) {
        return false;
    }

    if (resp_len < 6 || resp[0] == 0) {
        return false;
    }

    uint8_t id_len = resp[5];
    if (id_len > 10 || resp_len < 6 + id_len) {
        return false;
    }

    memcpy(uid, &resp[6], id_len);
    *uid_len = id_len;
    return true;
}

/**
 * @brief Thử kết nối với module PN532 qua giao thức UART HSU.
 */
static esp_err_t pn532_try_connect(void) {
    pn532_wakeup();

    uint32_t fw_version = 0;
    esp_err_t ret = pn532_get_firmware_version(&fw_version);
    if (ret != ESP_OK || ((fw_version >> 24) & 0xFF) != 0x32) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t ic  = (fw_version >> 24) & 0xFF;
    uint8_t ver = (fw_version >> 16) & 0xFF;
    uint8_t rev = (fw_version >> 8)  & 0xFF;
    uint8_t sup = fw_version & 0xFF;

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, ">>> [PN532 UART HSU] KET NOI THANH CONG! <<<");
    ESP_LOGI(TAG, ">>> Pin RX (nhan tu SDA): GPIO %d | Pin TX (phat vao SCL): GPIO %d", s_current_rx_pin, s_current_tx_pin);
    ESP_LOGI(TAG, ">>> Chip: PN5%02X | Firmware: v%d.%d | Ho tro: 0x%02X", ic, ver, rev, sup);
    ESP_LOGI(TAG, "==========================================================");

    pn532_sam_config();
    pn532_set_passive_activation_retries(0x01);

    s_is_connected = true;
    return ESP_OK;
}

/**
 * @brief Task FreeRTOS chạy ngầm quét thẻ RFID/NFC liên tục (Hỗ trợ Tự Đảo Chiều Chân).
 */
static void pn532_scanner_task(void *arg) {
    uint8_t last_uid[10] = {0};
    uint8_t last_uid_len = 0;
    int64_t last_seen_us = 0;
    int retry_count = 0;

    ESP_LOGI(TAG, "PN532 RFID/NFC UART Scanner Task running at 10Hz");

    while (1) {
        if (!s_scanner_active) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Tự động kết nối lại nếu chưa nhận diện
        if (!s_is_connected) {
            if (pn532_try_connect() == ESP_OK) {
                ESP_LOGI(TAG, ">>> PN532 DA SAN SANG QUET THE (UART HSU)! <<<");
            } else {
                retry_count++;
                
                // Tự động đảo chân RX <-> TX giữa GPIO 5 và GPIO 6 mỗi lần thử để chống cắm ngược!
                int temp = s_current_rx_pin;
                s_current_rx_pin = s_current_tx_pin;
                s_current_tx_pin = temp;
                uart_set_pin(PN532_UART_PORT, s_current_tx_pin, s_current_rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

                if (retry_count % 4 == 0) {
                    ESP_LOGW(TAG, "Dang tim PN532... [Hay dam bao DIP Switch: Switch 1 = 0 (OFF), Switch 2 = 0 (OFF)]");
                    ESP_LOGW(TAG, "                      [Dang thu TX=GPIO %d, RX=GPIO %d (Tu dong dao chan)]", s_current_tx_pin, s_current_rx_pin);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }

        uint8_t uid[10] = {0};
        uint8_t uid_len = 0;
        bool found = pn532_read_passive_target_id(uid, &uid_len);
        int64_t now_us = esp_timer_get_time();

        if (found && uid_len > 0) {
            bool is_same_card = (uid_len == last_uid_len && memcmp(uid, last_uid, uid_len) == 0);
            int64_t elapsed_ms = (now_us - last_seen_us) / 1000;

            if (!is_same_card || elapsed_ms > 1500) {
                memcpy(last_uid, uid, uid_len);
                last_uid_len = uid_len;
                last_seen_us = now_us;

                if (s_card_callback) {
                    s_card_callback(uid, uid_len);
                }
            } else {
                last_seen_us = now_us;
            }
        } else {
            if ((now_us - last_seen_us) > 400000LL) {
                last_uid_len = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Quét 10 lần mỗi giây
    }
}

esp_err_t pn532_driver_init(pn532_card_cb_t card_cb) {
    s_card_callback = card_cb;

    ESP_LOGI(TAG, "Initializing PN532 UART HSU Driver on GPIO %d and GPIO %d (Baud %d)...",
             PIN_PN532_GPIO_A, PIN_PN532_GPIO_B, PN532_UART_BAUD);

    uart_config_t uart_config = {
        .baud_rate = PN532_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(PN532_UART_PORT, &uart_config);
    if (ret != ESP_OK) return ret;

    ret = uart_set_pin(PN532_UART_PORT, s_current_tx_pin, s_current_rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return ret;

    ret = uart_driver_install(PN532_UART_PORT, 512, 512, 0, NULL, 0);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(100));

    // Thử kết nối lần đầu
    pn532_try_connect();

    // Khởi chạy task quét thẻ
    BaseType_t task_ret = xTaskCreate(pn532_scanner_task, "pn532_scan", 4096, NULL, 3, &s_scanner_task_handle);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create PN532 scanner task!");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool pn532_is_connected(void) {
    return s_is_connected;
}

void pn532_set_scanner_active(bool active) {
    s_scanner_active = active;
}
