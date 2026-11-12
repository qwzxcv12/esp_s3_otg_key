#include "pn532_driver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

static const char *TAG = "PN532_I2C";

#define PN532_PREAMBLE                      (0x00)
#define PN532_STARTCODE1                    (0x00)
#define PN532_STARTCODE2                    (0xFF)
#define PN532_POSTAMBLE                     (0x00)

#define PN532_HOSTTOPN532                   (0xD4)
#define PN532_PN532TOHOST                   (0xD5)

#define PN532_I2C_READY                     (0x01)

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
static int s_active_scl_pin = -1;
static int s_active_sda_pin = -1;

static void pn532_i2c_cleanup(void) {
    if (s_pn532_dev) {
        i2c_master_bus_rm_device(s_pn532_dev);
        s_pn532_dev = NULL;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    if (s_active_scl_pin >= 0) {
        gpio_reset_pin((gpio_num_t)s_active_scl_pin);
        s_active_scl_pin = -1;
    }
    if (s_active_sda_pin >= 0) {
        gpio_reset_pin((gpio_num_t)s_active_sda_pin);
        s_active_sda_pin = -1;
    }
}

static esp_err_t pn532_i2c_setup_bus(int scl_pin, int sda_pin) {
    pn532_i2c_cleanup();

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
        .scl_speed_hz = 50000, // 50kHz ổn định cao
        .scl_wait_us = 35000,  // 35ms cho clock stretching của PN532
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_pn532_dev);
    if (ret != ESP_OK) {
        pn532_i2c_cleanup();
        return ret;
    }

    s_active_scl_pin = scl_pin;
    s_active_sda_pin = sda_pin;
    return ESP_OK;
}

static bool pn532_i2c_wait_ready(uint32_t timeout_ms) {
    if (!s_pn532_dev) return false;
    int64_t start_time = esp_timer_get_time();
    int64_t timeout_us = (int64_t)timeout_ms * 1000;

    while ((esp_timer_get_time() - start_time) < timeout_us) {
        uint8_t status = 0;
        esp_err_t ret = i2c_master_receive(s_pn532_dev, &status, 1, 50);
        if (ret == ESP_OK && (status & 0x01) == PN532_I2C_READY) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static esp_err_t pn532_i2c_read_ack(uint32_t timeout_ms) {
    if (!pn532_i2c_wait_ready(timeout_ms)) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t ack_buf[7] = {0};
    esp_err_t ret = i2c_master_receive(s_pn532_dev, ack_buf, 7, 50);
    if (ret != ESP_OK) return ret;

    // ack_buf[0] là byte RDY (0x01), ack_buf[1..6] là chuỗi ACK chuẩn
    if (memcmp(&ack_buf[1], PN532_ACK, 6) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t pn532_i2c_write_command(const uint8_t *cmd, uint8_t cmd_len) {
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

    esp_err_t ret = i2c_master_transmit(s_pn532_dev, packet, cmd_len + 8, 100);
    if (ret != ESP_OK) return ret;

    // Đợi và xác nhận ACK giống Adafruit sendCommandCheckAck
    return pn532_i2c_read_ack(150);
}

static esp_err_t pn532_i2c_read_response(uint8_t expected_cmd, uint8_t *out_data, uint8_t *out_len, uint32_t timeout_ms) {
    if (!pn532_i2c_wait_ready(timeout_ms)) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t rx[64] = {0};
    esp_err_t ret = i2c_master_receive(s_pn532_dev, rx, sizeof(rx), 100);
    if (ret != ESP_OK) return ret;

    // Tìm start code 0x00 0xFF (bỏ qua byte RDY 0x01 ở đầu)
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
    if (tfi != PN532_PN532TOHOST || resp_cmd != (expected_cmd + 1)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t data_len = len - 2;
    if (out_data && out_len) {
        if (data_len > *out_len) data_len = *out_len;
        memcpy(out_data, &rx[start_idx + 6], data_len);
        *out_len = data_len;
    }
    return ESP_OK;
}

static esp_err_t pn532_get_firmware_version(uint32_t *version_out) {
    uint8_t cmd = PN532_CMD_GETFIRMWAREVERSION;
    esp_err_t ret = pn532_i2c_write_command(&cmd, 1);
    if (ret != ESP_OK) return ret;

    uint8_t resp[4] = {0};
    uint8_t resp_len = sizeof(resp);
    ret = pn532_i2c_read_response(PN532_CMD_GETFIRMWAREVERSION, resp, &resp_len, 300);
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
    uint8_t cmd[] = {PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01};
    esp_err_t ret = pn532_i2c_write_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;

    uint8_t resp[8] = {0};
    uint8_t resp_len = sizeof(resp);
    return pn532_i2c_read_response(PN532_CMD_SAMCONFIGURATION, resp, &resp_len, 200);
}

static esp_err_t pn532_set_passive_retries(uint8_t max_retries) {
    // RFCONFIGURATION: item 0x05 (MaxRetries: MxRtyATR, MxRtyPSL, MxRtyPassiveActivation)
    uint8_t cmd[] = {PN532_CMD_RFCONFIGURATION, 0x05, 0xFF, 0x01, max_retries};
    return pn532_i2c_write_command(cmd, sizeof(cmd));
}

static esp_err_t pn532_read_passive_target_id(uint8_t *uid, uint8_t *uid_len) {
    // 0x4A: InListPassiveTarget, MaxTargets=1, BrTy=0x00 (106 kbps ISO14443A Mifare)
    uint8_t cmd[] = {PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00};
    esp_err_t ret = pn532_i2c_write_command(cmd, sizeof(cmd));
    if (ret != ESP_OK) return ret;

    uint8_t resp[32] = {0};
    uint8_t resp_len = sizeof(resp);
    ret = pn532_i2c_read_response(PN532_CMD_INLISTPASSIVETARGET, resp, &resp_len, 100);
    if (ret != ESP_OK) return ret;

    // resp[0] là NbTg (số thẻ phát hiện)
    if (resp_len < 1 || resp[0] < 1) {
        return ESP_ERR_NOT_FOUND; // Không có thẻ
    }

    // resp: [0]=NbTg(1), [1]=Tg(1), [2]=SENS_RES, [3]=SENS_RES, [4]=SEL_RES, [5]=NFCIDLength, [6..]=UID
    if (resp_len < 6) return ESP_ERR_INVALID_SIZE;
    uint8_t id_len = resp[5];
    if (id_len == 0 || id_len > 7 || (6 + id_len) > resp_len) return ESP_ERR_INVALID_SIZE;

    if (uid && uid_len) {
        if (id_len > *uid_len) id_len = *uid_len;
        memcpy(uid, &resp[6], id_len);
        *uid_len = id_len;
    }
    return ESP_OK;
}

static esp_err_t pn532_try_init_config(int scl_pin, int sda_pin) {
    esp_err_t ret = pn532_i2c_setup_bus(scl_pin, sda_pin);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(30));

    uint32_t ver = 0;
    ret = pn532_get_firmware_version(&ver);
    if (ret == ESP_OK && ((ver >> 24) & 0xFF) == 0x32) {
        ESP_LOGI(TAG, ">>> KET NOI THANH CONG! Chip: PN5%02X, Firmware Ver: %d.%d, SCL=GPIO %d, SDA=GPIO %d <<<",
                 (unsigned int)((ver >> 24) & 0xFF),
                 (int)((ver >> 16) & 0xFF),
                 (int)((ver >> 8) & 0xFF),
                 scl_pin, sda_pin);

        pn532_sam_config();
        pn532_set_passive_retries(0x02); // 2 lần quét nhanh, không nghẽn loop
        s_is_connected = true;
        return ESP_OK;
    }
    return ESP_FAIL;
}

static void pn532_scanner_task(void *arg) {
    ESP_LOGI(TAG, "PN532 I2C Scanner Task Started on GPIO 5 & GPIO 6");

    int probe_step = 0;
    while (s_scanner_active) {
        if (!s_is_connected) {
            int scl = (probe_step % 2 == 0) ? PIN_PN532_GPIO_A : PIN_PN532_GPIO_B;
            int sda = (probe_step % 2 == 0) ? PIN_PN532_GPIO_B : PIN_PN532_GPIO_A;
            probe_step++;

            ESP_LOGI(TAG, "Dang tim PN532 qua I2C [SCL=GPIO %d, SDA=GPIO %d] (Dia chi 0x24)...", scl, sda);
            if (pn532_try_init_config(scl, sda) == ESP_OK) {
                ESP_LOGI(TAG, ">>> PN532 DA SAN SANG QUET THE NFC/RFID! <<<");
                continue;
            } else {
                int lvl_a = gpio_get_level((gpio_num_t)PIN_PN532_GPIO_A);
                int lvl_b = gpio_get_level((gpio_num_t)PIN_PN532_GPIO_B);
                ESP_LOGW(TAG, "Chua phan hoi... (Dien ap: GPIO5=%d, GPIO6=%d) -> Luu y: Can cong tac gat I0=1(ON), I1=0(OFF)",
                         lvl_a, lvl_b);
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        // Đã kết nối -> Quét thẻ NFC
        uint8_t uid[7] = {0};
        uint8_t uid_len = sizeof(uid);
        esp_err_t err = pn532_read_passive_target_id(uid, &uid_len);

        if (err == ESP_OK && uid_len > 0) {
            char uid_str[32] = {0};
            for (int i = 0; i < uid_len; i++) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X ", uid[i]);
                strcat(uid_str, hex);
            }
            ESP_LOGI(TAG, ">>> DA PHAT HIEN THE NFC! UID (%d bytes): %s<<<", uid_len, uid_str);

            if (s_card_callback) {
                s_card_callback(uid, uid_len);
            }

            // Tránh quét lặp liên tục khi giữ thẻ gần đầu đọc
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (err != ESP_ERR_NOT_FOUND && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Loi I2C trong khi quet (0x%x), thu khoi dong lai...", err);
            s_is_connected = false;
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }

    pn532_i2c_cleanup();
    vTaskDelete(NULL);
}

esp_err_t pn532_driver_init(pn532_card_cb_t card_cb) {
    s_card_callback = card_cb;

    ESP_LOGI(TAG, "Initializing PN532 I2C Driver (Adafruit protocol on GPIO 5 & GPIO 6)...");

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
