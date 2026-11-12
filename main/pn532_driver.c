#include "pn532_driver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"

static const char *TAG = "PN532";

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

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_pn532_dev = NULL;
static pn532_card_cb_t s_card_callback = NULL;
static TaskHandle_t s_scanner_task_handle = NULL;
static bool s_is_connected = false;
static bool s_scanner_active = true;

/**
 * @brief Kiểm tra xem PN532 đã sẵn sàng gửi dữ liệu chưa (I2C Ready Status bit 0 = 1).
 */
static bool pn532_wait_ready(uint32_t timeout_ms) {
    if (!s_pn532_dev) return false;

    uint32_t start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    while ((uint32_t)(esp_timer_get_time() / 1000) - start_ms < timeout_ms) {
        uint8_t status = 0;
        esp_err_t ret = i2c_master_receive(s_pn532_dev, &status, 1, 20);
        if (ret == ESP_OK && (status & 0x01) == 0x01) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

/**
 * @brief Đọc và kiểm tra gói tin ACK từ PN532.
 */
static esp_err_t pn532_read_ack(void) {
    if (!pn532_wait_ready(80)) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t ack_buf[7] = {0};
    esp_err_t ret = i2c_master_receive(s_pn532_dev, ack_buf, 7, 50);
    if (ret != ESP_OK) return ret;

    // Byte 0 là I2C Ready status (0x01), từ byte 1 đến 6 là ACK
    if (memcmp(&ack_buf[1], PN532_ACK, 6) != 0) {
        ESP_LOGD(TAG, "Invalid ACK frame received: %02X %02X %02X %02X %02X %02X",
                 ack_buf[1], ack_buf[2], ack_buf[3], ack_buf[4], ack_buf[5], ack_buf[6]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

/**
 * @brief Đóng gói và gửi lệnh xuống PN532 qua I2C.
 */
static esp_err_t pn532_write_command(const uint8_t *cmd, uint8_t cmd_len) {
    if (!s_pn532_dev) return ESP_ERR_INVALID_STATE;

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

    esp_err_t ret = i2c_master_transmit(s_pn532_dev, packet, cmd_len + 8, 50);
    if (ret != ESP_OK) {
        return ret;
    }

    return pn532_read_ack();
}

/**
 * @brief Đọc gói phản hồi từ PN532 sau khi gửi lệnh.
 */
static esp_err_t pn532_read_response(uint8_t expected_cmd, uint8_t *out_data, uint8_t *out_len, uint32_t timeout_ms) {
    if (!pn532_wait_ready(timeout_ms)) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t rx_buf[40] = {0};
    esp_err_t ret = i2c_master_receive(s_pn532_dev, rx_buf, sizeof(rx_buf), 50);
    if (ret != ESP_OK) return ret;

    // Tìm mã bắt đầu 0x00 0xFF trong bộ đệm nhận được
    int start_idx = -1;
    for (int i = 0; i < sizeof(rx_buf) - 5; i++) {
        if (rx_buf[i] == 0x00 && rx_buf[i + 1] == 0xFF) {
            start_idx = i;
            break;
        }
    }

    if (start_idx < 0) {
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

    uint8_t data_len = len - 2; // trừ TFI và Response Command
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
 * @brief Cấu hình SAM (Security Access Module) để kích hoạt từ trường RF.
 */
static esp_err_t pn532_sam_config(void) {
    // Mode = 0x01 (Normal), Timeout = 0x14 (1000ms), Use IRQ = 0x01
    uint8_t cmd[4] = {PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01};
    esp_err_t ret = pn532_write_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;

    uint8_t resp[2] = {0};
    uint8_t resp_len = sizeof(resp);
    return pn532_read_response(PN532_CMD_SAMCONFIGURATION, resp, &resp_len, 100);
}

/**
 * @brief Đặt số lần thử quét RF (MaxRetries) = 1 lần để không bị treo task khi không có thẻ.
 */
static esp_err_t pn532_set_passive_activation_retries(uint8_t max_retries) {
    // Config item 0x05 = MaxRetries
    uint8_t cmd[5] = {PN532_CMD_RFCONFIGURATION, 0x05, 0xFF, 0x01, max_retries};
    esp_err_t ret = pn532_write_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;

    uint8_t resp[2] = {0};
    uint8_t resp_len = sizeof(resp);
    return pn532_read_response(PN532_CMD_RFCONFIGURATION, resp, &resp_len, 100);
}

/**
 * @brief Quét tìm thẻ Type A (Mifare Classic, NTAG213/215/216...) trong phạm vi anten.
 */
static bool pn532_read_passive_target_id(uint8_t *uid, uint8_t *uid_len) {
    // 0x4A: MaxTg = 1, BrTy = 0 (106 kbps ISO/IEC 14443 Type A)
    uint8_t cmd[3] = {PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00};
    if (pn532_write_command(cmd, sizeof(cmd)) != ESP_OK) {
        return false;
    }

    uint8_t resp[24] = {0};
    uint8_t resp_len = sizeof(resp);
    if (pn532_read_response(PN532_CMD_INLISTPASSIVETARGET, resp, &resp_len, 60) != ESP_OK) {
        return false;
    }

    // resp[0] là số lượng thẻ phát hiện (NbTg)
    if (resp_len < 6 || resp[0] == 0) {
        return false; // Không có thẻ nào trong vùng phủ sóng
    }

    // resp[1] = Tg (1)
    // resp[2..3] = SENS_RES (ATQA)
    // resp[4] = SEL_RES (SAK)
    // resp[5] = NFCIDLength (Độ dài UID: thường là 4 hoặc 7 byte)
    uint8_t id_len = resp[5];
    if (id_len > 10 || resp_len < 6 + id_len) {
        return false;
    }

    memcpy(uid, &resp[6], id_len);
    *uid_len = id_len;
    return true;
}

/**
 * @brief Task FreeRTOS chạy ngầm quét thẻ RFID/NFC liên tục.
 */
static void pn532_scanner_task(void *arg) {
    uint8_t last_uid[10] = {0};
    uint8_t last_uid_len = 0;
    int64_t last_seen_us = 0;

    ESP_LOGI(TAG, "PN532 RFID/NFC Background Scanner Task running at 10Hz");

    while (1) {
        if (!s_scanner_active || !s_is_connected) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        uint8_t uid[10] = {0};
        uint8_t uid_len = 0;
        bool found = pn532_read_passive_target_id(uid, &uid_len);
        int64_t now_us = esp_timer_get_time();

        if (found && uid_len > 0) {
            // Kiểm tra chống quét lặp: Nếu giữ thẻ liên tục trên đầu đọc thì chỉ kích hoạt 1 lần
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
                // Cập nhật lại thời gian nhìn thấy thẻ để giữ debounce khi thẻ đang được giữ yên
                last_seen_us = now_us;
            }
        } else {
            // Không còn thẻ trong vùng quét: Nếu thẻ đã nhấc ra hơn 400ms thì reset trạng thái
            if ((now_us - last_seen_us) > 400000LL) {
                last_uid_len = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Quét 10 lần mỗi giây
    }
}

esp_err_t pn532_driver_init(pn532_card_cb_t card_cb) {
    s_card_callback = card_cb;

    ESP_LOGI(TAG, "Initializing PN532 I2C Driver on SDA=GPIO %d, SCL=GPIO %d (Addr 0x%02X)...",
             PIN_PN532_SDA, PIN_PN532_SCL, PN532_I2C_ADDR);

    // 1. Cấu hình I2C Master Bus (ESP-IDF v5.3 i2c_master API)
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)PIN_PN532_SDA,
        .scl_io_num = (gpio_num_t)PIN_PN532_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C Master Bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Thêm thiết bị PN532 vào Bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PN532_I2C_ADDR,
        .scl_speed_hz = 100000, // 100 kHz chuẩn công nghiệp cho PN532
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_pn532_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add PN532 I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Chờ 100ms để phần cứng PN532 khởi động ổn định
    vTaskDelay(pdMS_TO_TICKS(100));

    // 3. Đọc Firmware Version của PN532
    uint32_t fw_version = 0;
    ret = pn532_get_firmware_version(&fw_version);
    if (ret != ESP_OK || ((fw_version >> 24) & 0xFF) != 0x32) {
        ESP_LOGW(TAG, "PN532 not responding or invalid chip! (Ret: %s, Ver: 0x%08lX)",
                 esp_err_to_name(ret), (unsigned long)fw_version);
        ESP_LOGW(TAG, "-> Hãy kiểm tra: 1) Dây SDA=GPIO 6, SCL=GPIO 5; 2) Công tắc gạt DIP Switch I0=0, I1=1");
        s_is_connected = false;
        // Không assert/crash hệ thống, để các tính năng khác của đàn vẫn chạy bình thường
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t ic  = (fw_version >> 24) & 0xFF;
    uint8_t ver = (fw_version >> 16) & 0xFF;
    uint8_t rev = (fw_version >> 8)  & 0xFF;
    uint8_t sup = fw_version & 0xFF;

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, ">>> PN532 NFC/RFID MODULE PHÁT HIỆN THÀNH CÔNG! <<<");
    ESP_LOGI(TAG, ">>> Chip: PN5%02X | Firmware: v%d.%d | Hỗ trợ: 0x%02X", ic, ver, rev, sup);
    ESP_LOGI(TAG, "=================================================");

    // 4. Kích hoạt anten RF (SAM Configuration)
    ret = pn532_sam_config();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure SAM: %s", esp_err_to_name(ret));
    }

    // 5. Cài đặt MaxRetries = 1 lần để hàm InListPassiveTarget trả về ngay khi không có thẻ
    ret = pn532_set_passive_activation_retries(0x01);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set MaxRetries: %s", esp_err_to_name(ret));
    }

    s_is_connected = true;

    // 6. Khởi chạy Scanner Task ngầm (Priority 3, Stack 4096)
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
