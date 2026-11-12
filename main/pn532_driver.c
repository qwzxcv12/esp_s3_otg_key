#include "pn532_driver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

static const char *TAG = "PN532";

#define PN532_PREAMBLE                      (0x00)
#define PN532_STARTCODE1                    (0x00)
#define PN532_STARTCODE2                    (0xFF)
#define PN532_POSTAMBLE                     (0x00)

#define PN532_HOSTTOPN532                   (0xD4)
#define PN532_PN532TOHOST                   (0xD5)

#define PN532_I2C_ADDRESS                   (0x24)
#define PN532_I2C_READY                     (0x01)

// PN532 Commands
#define PN532_CMD_GETFIRMWAREVERSION        (0x02)
#define PN532_CMD_SAMCONFIGURATION          (0x14)
#define PN532_CMD_RFCONFIGURATION          (0x32)
#define PN532_CMD_INLISTPASSIVETARGET       (0x4A)

// ACK Frame: 0x00 0x00 0xFF 0x00 0xFF 0x00
static const uint8_t PN532_ACK[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

typedef enum {
    PN532_TRANSPORT_NONE = 0,
    PN532_TRANSPORT_UART,
    PN532_TRANSPORT_I2C,
} pn532_transport_t;

static pn532_transport_t s_transport = PN532_TRANSPORT_NONE;
static pn532_card_cb_t s_card_callback = NULL;
static TaskHandle_t s_scanner_task_handle = NULL;
static bool s_is_connected = false;
static bool s_scanner_active = true;

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_pn532_i2c_dev = NULL;
static bool s_uart_installed = false;

static void cleanup_hardware(void) {
    if (s_pn532_i2c_dev) {
        i2c_master_bus_rm_device(s_pn532_i2c_dev);
        s_pn532_i2c_dev = NULL;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    if (s_uart_installed) {
        uart_driver_delete(PN532_UART_PORT);
        s_uart_installed = false;
    }
    gpio_reset_pin((gpio_num_t)PIN_PN532_GPIO_A);
    gpio_reset_pin((gpio_num_t)PIN_PN532_GPIO_B);
}

static esp_err_t setup_uart(int tx_pin, int rx_pin) {
    cleanup_hardware();
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
    ret = uart_set_pin(PN532_UART_PORT, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return ret;
    ret = uart_driver_install(PN532_UART_PORT, 512, 512, 0, NULL, 0);
    if (ret == ESP_OK) s_uart_installed = true;
    return ret;
}

static esp_err_t setup_i2c(int scl_pin, int sda_pin) {
    cleanup_hardware();
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)sda_pin,
        .scl_io_num = (gpio_num_t)scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) return ret;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PN532_I2C_ADDRESS,
        .scl_speed_hz = 50000, // 50kHz ổn định
    };
    return i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_pn532_i2c_dev);
}

static void pn532_wakeup_uart(void) {
    // Chuẩn NXP UM0701-04 HSU Wakeup
    uint8_t w[16] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uart_write_bytes(PN532_UART_PORT, (const char *)w, sizeof(w));
    vTaskDelay(pdMS_TO_TICKS(25));
    uart_flush_input(PN532_UART_PORT);
}

static esp_err_t pn532_read_ack_uart(uint32_t timeout_ms) {
    uint8_t match_idx = 0;
    int64_t start_time = esp_timer_get_time();
    int64_t timeout_us = (int64_t)timeout_ms * 1000;

    while ((esp_timer_get_time() - start_time) < timeout_us) {
        uint8_t ch = 0;
        int r = uart_read_bytes(PN532_UART_PORT, &ch, 1, pdMS_TO_TICKS(10));
        if (r > 0) {
            if (ch == PN532_ACK[match_idx]) {
                match_idx++;
                if (match_idx == sizeof(PN532_ACK)) {
                    return ESP_OK;
                }
            } else {
                if (ch == PN532_ACK[0]) {
                    match_idx = 1;
                } else {
                    match_idx = 0;
                }
            }
        }
    }
    return ESP_ERR_TIMEOUT;
}

static bool pn532_i2c_wait_ready(uint32_t timeout_ms) {
    if (!s_pn532_i2c_dev) return false;
    int64_t start = esp_timer_get_time();
    int64_t timeout_us = (int64_t)timeout_ms * 1000;
    while ((esp_timer_get_time() - start) < timeout_us) {
        uint8_t status = 0;
        esp_err_t ret = i2c_master_receive(s_pn532_i2c_dev, &status, 1, 50);
        if (ret == ESP_OK && (status & 0x01) == PN532_I2C_READY) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static esp_err_t pn532_read_ack_i2c(uint32_t timeout_ms) {
    if (!pn532_i2c_wait_ready(timeout_ms)) return ESP_ERR_TIMEOUT;
    uint8_t ack_buf[7] = {0};
    esp_err_t ret = i2c_master_receive(s_pn532_i2c_dev, ack_buf, 7, 50);
    if (ret != ESP_OK) return ret;
    if (memcmp(&ack_buf[1], PN532_ACK, 6) != 0) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

static esp_err_t pn532_write_command(const uint8_t *cmd, uint8_t cmd_len) {
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

    if (s_transport == PN532_TRANSPORT_UART) {
        uart_flush_input(PN532_UART_PORT);
        uart_write_bytes(PN532_UART_PORT, (const char *)packet, cmd_len + 8);
        return pn532_read_ack_uart(250);
    } else if (s_transport == PN532_TRANSPORT_I2C) {
        if (!s_pn532_i2c_dev) return ESP_ERR_INVALID_STATE;
        esp_err_t ret = i2c_master_transmit(s_pn532_i2c_dev, packet, cmd_len + 8, 100);
        if (ret != ESP_OK) return ret;
        return pn532_read_ack_i2c(150);
    }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t pn532_read_response_uart(uint8_t expected_cmd, uint8_t *out_data, uint8_t *out_len, uint32_t timeout_ms) {
    int64_t start_time = esp_timer_get_time();
    int64_t timeout_us = (int64_t)timeout_ms * 1000;

    // 1. Tìm start code 0x00 0xFF
    uint8_t prev = 0xFF;
    bool found_start = false;
    while ((esp_timer_get_time() - start_time) < timeout_us) {
        uint8_t ch = 0;
        int r = uart_read_bytes(PN532_UART_PORT, &ch, 1, pdMS_TO_TICKS(10));
        if (r > 0) {
            if (prev == 0x00 && ch == 0xFF) {
                found_start = true;
                break;
            }
            prev = ch;
        }
    }
    if (!found_start) return ESP_ERR_TIMEOUT;

    // 2. Đọc LEN và LCS
    uint8_t len_lcs[2] = {0};
    if (uart_read_bytes(PN532_UART_PORT, len_lcs, 2, pdMS_TO_TICKS(50)) < 2) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t len = len_lcs[0];
    uint8_t lcs = len_lcs[1];
    if ((uint8_t)(len + lcs) != 0) return ESP_ERR_INVALID_CRC;
    if (len < 2) return ESP_ERR_INVALID_SIZE;

    // 3. Đọc dữ liệu (TFI, CMD, Payload)
    uint8_t payload[64];
    if (len > sizeof(payload)) return ESP_ERR_NO_MEM;
    if (uart_read_bytes(PN532_UART_PORT, payload, len, pdMS_TO_TICKS(100)) < len) {
        return ESP_ERR_TIMEOUT;
    }

    // 4. Đọc DCS và Postamble
    uint8_t dcs_post[2] = {0};
    if (uart_read_bytes(PN532_UART_PORT, dcs_post, 2, pdMS_TO_TICKS(50)) < 1) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t dcs = dcs_post[0];

    uint8_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += payload[i];
    }
    if ((uint8_t)(sum + dcs) != 0) return ESP_ERR_INVALID_CRC;

    uint8_t tfi = payload[0];
    uint8_t resp_cmd = payload[1];
    if (tfi != PN532_PN532TOHOST || resp_cmd != (expected_cmd + 1)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t data_len = len - 2;
    if (out_data && out_len) {
        if (data_len > *out_len) data_len = *out_len;
        memcpy(out_data, &payload[2], data_len);
        *out_len = data_len;
    }

    return ESP_OK;
}

static esp_err_t pn532_read_response_i2c(uint8_t expected_cmd, uint8_t *out_data, uint8_t *out_len, uint32_t timeout_ms) {
    if (!pn532_i2c_wait_ready(timeout_ms)) return ESP_ERR_TIMEOUT;
    uint8_t rx[64] = {0};
    esp_err_t ret = i2c_master_receive(s_pn532_i2c_dev, rx, sizeof(rx), 100);
    if (ret != ESP_OK) return ret;

    int start_idx = -1;
    for (int i = 0; i <= (int)sizeof(rx) - 6; i++) {
        if (rx[i] == 0x00 && rx[i + 1] == 0xFF) {
            start_idx = i;
            break;
        }
    }
    if (start_idx < 0) return ESP_ERR_NOT_FOUND;

    uint8_t len = rx[start_idx + 2];
    uint8_t lcs = rx[start_idx + 3];
    if ((uint8_t)(len + lcs) != 0) return ESP_ERR_INVALID_CRC;
    if (len < 2) return ESP_ERR_INVALID_SIZE;

    uint8_t tfi = rx[start_idx + 4];
    uint8_t resp_cmd = rx[start_idx + 5];
    if (tfi != PN532_PN532TOHOST || resp_cmd != (expected_cmd + 1)) return ESP_ERR_INVALID_RESPONSE;

    uint8_t data_len = len - 2;
    if (out_data && out_len) {
        if (data_len > *out_len) data_len = *out_len;
        memcpy(out_data, &rx[start_idx + 6], data_len);
        *out_len = data_len;
    }
    return ESP_OK;
}

static esp_err_t pn532_read_response(uint8_t expected_cmd, uint8_t *out_data, uint8_t *out_len, uint32_t timeout_ms) {
    if (s_transport == PN532_TRANSPORT_UART) {
        return pn532_read_response_uart(expected_cmd, out_data, out_len, timeout_ms);
    } else if (s_transport == PN532_TRANSPORT_I2C) {
        return pn532_read_response_i2c(expected_cmd, out_data, out_len, timeout_ms);
    }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t pn532_get_firmware_version(uint32_t *version_out) {
    uint8_t cmd = PN532_CMD_GETFIRMWAREVERSION;
    esp_err_t ret = pn532_write_command(&cmd, 1);
    if (ret != ESP_OK) return ret;

    uint8_t resp[4] = {0};
    uint8_t resp_len = sizeof(resp);
    ret = pn532_read_response(PN532_CMD_GETFIRMWAREVERSION, resp, &resp_len, 500);
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

static esp_err_t pn532_sam_config(void) {
    uint8_t cmd[4] = {PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01};
    esp_err_t ret = pn532_write_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;

    uint8_t resp[2] = {0};
    uint8_t resp_len = sizeof(resp);
    return pn532_read_response(PN532_CMD_SAMCONFIGURATION, resp, &resp_len, 200);
}

static esp_err_t pn532_set_passive_activation_retries(uint8_t max_retries) {
    uint8_t cmd[5] = {PN532_CMD_RFCONFIGURATION, 0x05, 0xFF, 0x01, max_retries};
    esp_err_t ret = pn532_write_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;

    uint8_t resp[2] = {0};
    uint8_t resp_len = sizeof(resp);
    return pn532_read_response(PN532_CMD_RFCONFIGURATION, resp, &resp_len, 200);
}

static bool pn532_read_passive_target_id(uint8_t *uid, uint8_t *uid_len) {
    uint8_t cmd[3] = {PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00};
    if (pn532_write_command(cmd, sizeof(cmd)) != ESP_OK) {
        return false;
    }

    uint8_t resp[24] = {0};
    uint8_t resp_len = sizeof(resp);
    if (pn532_read_response(PN532_CMD_INLISTPASSIVETARGET, resp, &resp_len, 100) != ESP_OK) {
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

static esp_err_t pn532_try_connect_uart(int tx_pin, int rx_pin) {
    esp_err_t ret = setup_uart(tx_pin, rx_pin);
    if (ret != ESP_OK) return ret;
    s_transport = PN532_TRANSPORT_UART;
    pn532_wakeup_uart();

    uint32_t fw_version = 0;
    ret = pn532_get_firmware_version(&fw_version);
    if (ret != ESP_OK || ((fw_version >> 24) & 0xFF) != 0x32) {
        s_transport = PN532_TRANSPORT_NONE;
        cleanup_hardware();
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t ic  = (fw_version >> 24) & 0xFF;
    uint8_t ver = (fw_version >> 16) & 0xFF;
    uint8_t rev = (fw_version >> 8)  & 0xFF;
    uint8_t sup = fw_version & 0xFF;

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, ">>> [PN532 UART HSU] KET NOI THANH CONG! <<<");
    ESP_LOGI(TAG, ">>> ESP32 TX=GPIO %d | ESP32 RX=GPIO %d", tx_pin, rx_pin);
    ESP_LOGI(TAG, ">>> Chip: PN5%02X | Firmware: v%d.%d | Ho tro: 0x%02X", ic, ver, rev, sup);
    ESP_LOGI(TAG, "==========================================================");

    pn532_sam_config();
    pn532_set_passive_activation_retries(0x01);
    s_is_connected = true;
    return ESP_OK;
}

static esp_err_t pn532_try_connect_i2c(int scl_pin, int sda_pin) {
    esp_err_t ret = setup_i2c(scl_pin, sda_pin);
    if (ret != ESP_OK) return ret;
    s_transport = PN532_TRANSPORT_I2C;

    uint32_t fw_version = 0;
    ret = pn532_get_firmware_version(&fw_version);
    if (ret != ESP_OK || ((fw_version >> 24) & 0xFF) != 0x32) {
        s_transport = PN532_TRANSPORT_NONE;
        cleanup_hardware();
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t ic  = (fw_version >> 24) & 0xFF;
    uint8_t ver = (fw_version >> 16) & 0xFF;
    uint8_t rev = (fw_version >> 8)  & 0xFF;
    uint8_t sup = fw_version & 0xFF;

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, ">>> [PN532 I2C MASTER] KET NOI THANH CONG! <<<");
    ESP_LOGI(TAG, ">>> ESP32 SCL=GPIO %d | ESP32 SDA=GPIO %d", scl_pin, sda_pin);
    ESP_LOGI(TAG, ">>> Chip: PN5%02X | Firmware: v%d.%d | Ho tro: 0x%02X", ic, ver, rev, sup);
    ESP_LOGI(TAG, "==========================================================");

    pn532_sam_config();
    pn532_set_passive_activation_retries(0x01);
    s_is_connected = true;
    return ESP_OK;
}

static void pn532_scanner_task(void *arg) {
    uint8_t last_uid[10] = {0};
    uint8_t last_uid_len = 0;
    int64_t last_seen_us = 0;
    int probe_step = 0;

    ESP_LOGI(TAG, "PN532 Dual-Mode (UART/I2C Auto-Negotiate) Task started");

    while (1) {
        if (!s_scanner_active) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (s_transport == PN532_TRANSPORT_NONE) {
            esp_err_t conn_err = ESP_FAIL;
            int step = (probe_step++) % 4;

            if (step == 0) {
                ESP_LOGI(TAG, "Dang do PN532 qua UART (ESP32 TX=GPIO 5, RX=GPIO 6)...");
                conn_err = pn532_try_connect_uart(PIN_PN532_GPIO_A, PIN_PN532_GPIO_B);
            } else if (step == 1) {
                ESP_LOGI(TAG, "Dang do PN532 qua UART dao chieu (ESP32 TX=GPIO 6, RX=GPIO 5)...");
                conn_err = pn532_try_connect_uart(PIN_PN532_GPIO_B, PIN_PN532_GPIO_A);
            } else if (step == 2) {
                ESP_LOGI(TAG, "Dang do PN532 qua I2C (ESP32 SCL=GPIO 5, SDA=GPIO 6)...");
                conn_err = pn532_try_connect_i2c(PIN_PN532_GPIO_A, PIN_PN532_GPIO_B);
            } else if (step == 3) {
                ESP_LOGI(TAG, "Dang do PN532 qua I2C dao chieu (ESP32 SCL=GPIO 6, SDA=GPIO 5)...");
                conn_err = pn532_try_connect_i2c(PIN_PN532_GPIO_B, PIN_PN532_GPIO_A);
            }

            if (conn_err == ESP_OK) {
                ESP_LOGI(TAG, ">>> PN532 KET NOI VA KHOI DONG HOAN TAT! SAN SANG QUET THE! <<<");
            } else {
                vTaskDelay(pdMS_TO_TICKS(1500));
            }
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

    ESP_LOGI(TAG, "Initializing PN532 Universal Driver (Auto-Detect UART & I2C on GPIO 5 & 6)...");

    // Khởi chạy task dò tìm và quét thẻ ngầm
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
