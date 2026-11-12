/*
 * PN532 Universal Driver — ESP-IDF
 * Tự động dò tìm PN532 qua cả I2C VÀ UART HSU trên GPIO 5 & GPIO 6
 * với mọi chiều dây và mọi vị trí công tắc DIP.
 *
 * Thứ tự dò (4 bước, lặp vòng):
 *   0. UART HSU: TX=GPIO5, RX=GPIO6
 *   1. UART HSU: TX=GPIO6, RX=GPIO5
 *   2. I2C:      SCL=GPIO5, SDA=GPIO6
 *   3. I2C:      SCL=GPIO6, SDA=GPIO5
 */

#include "pn532_driver.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"

static const char *TAG = "PN532";

#define PN532_PREAMBLE                      (0x00)
#define PN532_STARTCODE1                    (0x00)
#define PN532_STARTCODE2                    (0xFF)
#define PN532_POSTAMBLE                     (0x00)
#define PN532_HOSTTOPN532                   (0xD4)
#define PN532_PN532TOHOST                   (0xD5)
#define PN532_I2C_READY                     (0x01)

#define PN532_CMD_GETFIRMWAREVERSION        (0x02)
#define PN532_CMD_SAMCONFIGURATION          (0x14)
#define PN532_CMD_RFCONFIGURATION           (0x32)
#define PN532_CMD_INLISTPASSIVETARGET       (0x4A)

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

// I2C state
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_pn532_dev = NULL;

// UART state
static bool s_uart_installed = false;

// ===========================  CLEANUP  ===========================

static void cleanup_all(void) {
    if (s_pn532_dev) { i2c_master_bus_rm_device(s_pn532_dev); s_pn532_dev = NULL; }
    if (s_i2c_bus)   { i2c_del_master_bus(s_i2c_bus);          s_i2c_bus = NULL; }
    if (s_uart_installed) { uart_driver_delete(PN532_UART_PORT); s_uart_installed = false; }
    gpio_reset_pin((gpio_num_t)PIN_PN532_GPIO_A);
    gpio_reset_pin((gpio_num_t)PIN_PN532_GPIO_B);
    s_transport = PN532_TRANSPORT_NONE;
}

// =========================  UART helpers  =========================

static esp_err_t setup_uart(int tx_pin, int rx_pin) {
    cleanup_all();
    uart_config_t cfg = {
        .baud_rate = PN532_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret = uart_param_config(PN532_UART_PORT, &cfg);
    if (ret != ESP_OK) return ret;
    ret = uart_set_pin(PN532_UART_PORT, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return ret;
    ret = uart_driver_install(PN532_UART_PORT, 512, 512, 0, NULL, 0);
    if (ret == ESP_OK) { s_uart_installed = true; s_transport = PN532_TRANSPORT_UART; }
    return ret;
}

static void uart_wakeup(void) {
    uint8_t w[16] = {0x55,0x55,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    uart_write_bytes(PN532_UART_PORT, (const char *)w, sizeof(w));
    vTaskDelay(pdMS_TO_TICKS(25));
    uart_flush_input(PN532_UART_PORT);
}

static esp_err_t uart_read_ack(uint32_t timeout_ms) {
    uint8_t idx = 0;
    int64_t start = esp_timer_get_time();
    int64_t tmo = (int64_t)timeout_ms * 1000;
    while ((esp_timer_get_time() - start) < tmo) {
        uint8_t ch = 0;
        if (uart_read_bytes(PN532_UART_PORT, &ch, 1, pdMS_TO_TICKS(10)) > 0) {
            if (ch == PN532_ACK[idx]) { if (++idx == 6) return ESP_OK; }
            else idx = (ch == PN532_ACK[0]) ? 1 : 0;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t uart_read_response(uint8_t expected_cmd, uint8_t *out, uint8_t *out_len, uint32_t timeout_ms) {
    int64_t start = esp_timer_get_time();
    int64_t tmo = (int64_t)timeout_ms * 1000;
    uint8_t prev = 0xFF; bool found = false;
    while ((esp_timer_get_time() - start) < tmo) {
        uint8_t ch;
        if (uart_read_bytes(PN532_UART_PORT, &ch, 1, pdMS_TO_TICKS(10)) > 0) {
            if (prev == 0x00 && ch == 0xFF) { found = true; break; }
            prev = ch;
        }
    }
    if (!found) return ESP_ERR_TIMEOUT;
    uint8_t ll[2];
    if (uart_read_bytes(PN532_UART_PORT, ll, 2, pdMS_TO_TICKS(50)) < 2) return ESP_ERR_TIMEOUT;
    if ((uint8_t)(ll[0]+ll[1]) != 0) return ESP_ERR_INVALID_CRC;
    if (ll[0] < 2) return ESP_ERR_INVALID_SIZE;
    uint8_t payload[64]; uint8_t plen = ll[0];
    if (plen > sizeof(payload)) return ESP_ERR_NO_MEM;
    if (uart_read_bytes(PN532_UART_PORT, payload, plen, pdMS_TO_TICKS(100)) < plen) return ESP_ERR_TIMEOUT;
    uint8_t dc[2]; uart_read_bytes(PN532_UART_PORT, dc, 2, pdMS_TO_TICKS(50));
    uint8_t sum = 0; for (int i=0;i<plen;i++) sum+=payload[i];
    if ((uint8_t)(sum+dc[0])!=0) return ESP_ERR_INVALID_CRC;
    if (payload[0]!=PN532_PN532TOHOST || payload[1]!=(expected_cmd+1)) return ESP_ERR_INVALID_RESPONSE;
    uint8_t dlen = plen-2;
    if (out && out_len) { if (dlen>*out_len) dlen=*out_len; memcpy(out,&payload[2],dlen); *out_len=dlen; }
    return ESP_OK;
}

// =========================  I2C helpers  =========================

static esp_err_t setup_i2c(int scl_pin, int sda_pin) {
    cleanup_all();
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
        .scl_speed_hz = 50000,
        .scl_wait_us = 35000,
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_pn532_dev);
    if (ret != ESP_OK) { cleanup_all(); return ret; }
    s_transport = PN532_TRANSPORT_I2C;
    return ESP_OK;
}

static bool i2c_wait_ready(uint32_t timeout_ms) {
    if (!s_pn532_dev) return false;
    int64_t start = esp_timer_get_time();
    int64_t tmo = (int64_t)timeout_ms * 1000;
    while ((esp_timer_get_time()-start) < tmo) {
        uint8_t st = 0;
        if (i2c_master_receive(s_pn532_dev,&st,1,50)==ESP_OK && (st&1)==PN532_I2C_READY) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static esp_err_t i2c_read_ack(uint32_t timeout_ms) {
    if (!i2c_wait_ready(timeout_ms)) return ESP_ERR_TIMEOUT;
    uint8_t buf[7]={0};
    esp_err_t ret = i2c_master_receive(s_pn532_dev,buf,7,50);
    if (ret!=ESP_OK) return ret;
    return (memcmp(&buf[1],PN532_ACK,6)==0) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t i2c_read_response(uint8_t expected_cmd, uint8_t *out, uint8_t *out_len, uint32_t timeout_ms) {
    if (!i2c_wait_ready(timeout_ms)) return ESP_ERR_TIMEOUT;
    uint8_t rx[64]={0};
    esp_err_t ret = i2c_master_receive(s_pn532_dev,rx,sizeof(rx),100);
    if (ret!=ESP_OK) return ret;
    int si=-1;
    for (int i=0;i<=(int)sizeof(rx)-6;i++) { if (rx[i]==0x00&&rx[i+1]==0xFF){si=i;break;} }
    if (si<0) return ESP_ERR_NOT_FOUND;
    uint8_t len=rx[si+2], lcs=rx[si+3];
    if ((uint8_t)(len+lcs)!=0) return ESP_ERR_INVALID_CRC;
    if (len<2) return ESP_ERR_INVALID_SIZE;
    if (rx[si+4]!=PN532_PN532TOHOST || rx[si+5]!=(expected_cmd+1)) return ESP_ERR_INVALID_RESPONSE;
    uint8_t dlen=len-2;
    if (out&&out_len) { if(dlen>*out_len) dlen=*out_len; memcpy(out,&rx[si+6],dlen); *out_len=dlen; }
    return ESP_OK;
}

// ===================  Unified command layer  =====================

static esp_err_t pn532_write_command(const uint8_t *cmd, uint8_t cmd_len) {
    uint8_t pkt[64];
    if (cmd_len+8 > sizeof(pkt)) return ESP_ERR_INVALID_SIZE;
    uint8_t len = cmd_len+1;
    pkt[0]=PN532_PREAMBLE; pkt[1]=PN532_STARTCODE1; pkt[2]=PN532_STARTCODE2;
    pkt[3]=len; pkt[4]=(uint8_t)(~len+1); pkt[5]=PN532_HOSTTOPN532;
    uint8_t sum=PN532_HOSTTOPN532;
    for (int i=0;i<cmd_len;i++) { pkt[6+i]=cmd[i]; sum+=cmd[i]; }
    pkt[6+cmd_len]=(uint8_t)(~sum+1); pkt[7+cmd_len]=PN532_POSTAMBLE;

    if (s_transport==PN532_TRANSPORT_UART) {
        uart_flush_input(PN532_UART_PORT);
        uart_write_bytes(PN532_UART_PORT,(const char*)pkt,cmd_len+8);
        return uart_read_ack(250);
    } else if (s_transport==PN532_TRANSPORT_I2C) {
        esp_err_t ret = i2c_master_transmit(s_pn532_dev,pkt,cmd_len+8,100);
        if (ret!=ESP_OK) return ret;
        return i2c_read_ack(150);
    }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t pn532_read_response(uint8_t expected_cmd, uint8_t *out, uint8_t *out_len, uint32_t timeout_ms) {
    if (s_transport==PN532_TRANSPORT_UART) return uart_read_response(expected_cmd,out,out_len,timeout_ms);
    if (s_transport==PN532_TRANSPORT_I2C)  return i2c_read_response(expected_cmd,out,out_len,timeout_ms);
    return ESP_ERR_INVALID_STATE;
}

// ====================  High-level commands  ======================

static esp_err_t pn532_get_firmware_version(uint32_t *ver) {
    uint8_t cmd = PN532_CMD_GETFIRMWAREVERSION;
    esp_err_t ret = pn532_write_command(&cmd,1);
    if (ret!=ESP_OK) return ret;
    uint8_t r[4]={0}; uint8_t rl=4;
    ret = pn532_read_response(PN532_CMD_GETFIRMWAREVERSION,r,&rl,500);
    if (ret==ESP_OK && rl>=4 && ver)
        *ver = ((uint32_t)r[0]<<24)|((uint32_t)r[1]<<16)|((uint32_t)r[2]<<8)|r[3];
    return ret;
}

static esp_err_t pn532_sam_config(void) {
    uint8_t cmd[]={PN532_CMD_SAMCONFIGURATION,0x01,0x14,0x01};
    esp_err_t ret = pn532_write_command(cmd,sizeof(cmd));
    if (ret!=ESP_OK) return ret;
    uint8_t r[8]; uint8_t rl=sizeof(r);
    return pn532_read_response(PN532_CMD_SAMCONFIGURATION,r,&rl,200);
}

static esp_err_t pn532_set_passive_retries(uint8_t retries) {
    uint8_t cmd[]={PN532_CMD_RFCONFIGURATION,0x05,0xFF,0x01,retries};
    return pn532_write_command(cmd,sizeof(cmd));
}

static esp_err_t pn532_read_passive_target(uint8_t *uid, uint8_t *uid_len) {
    uint8_t cmd[]={PN532_CMD_INLISTPASSIVETARGET,0x01,0x00};
    esp_err_t ret = pn532_write_command(cmd,sizeof(cmd));
    if (ret!=ESP_OK) return ret;
    uint8_t resp[32]={0}; uint8_t rl=sizeof(resp);
    ret = pn532_read_response(PN532_CMD_INLISTPASSIVETARGET,resp,&rl,100);
    if (ret!=ESP_OK) return ret;
    if (rl<1||resp[0]<1) return ESP_ERR_NOT_FOUND;
    if (rl<6) return ESP_ERR_INVALID_SIZE;
    uint8_t id_len=resp[5];
    if (id_len==0||id_len>7||(6+id_len)>rl) return ESP_ERR_INVALID_SIZE;
    if (uid&&uid_len) { if(id_len>*uid_len) id_len=*uid_len; memcpy(uid,&resp[6],id_len); *uid_len=id_len; }
    return ESP_OK;
}

// ====================  Connection probes  ========================

static esp_err_t try_connect_uart(int tx_pin, int rx_pin) {
    esp_err_t ret = setup_uart(tx_pin, rx_pin);
    if (ret != ESP_OK) return ret;
    uart_wakeup();
    vTaskDelay(pdMS_TO_TICKS(5));
    uint32_t ver = 0;
    for (int i = 0; i < 2; i++) {
        uart_wakeup();
        ret = pn532_get_firmware_version(&ver);
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    if (ret == ESP_OK && ((ver >> 24) & 0xFF) == 0x32) {
        ESP_LOGI(TAG, ">>> KET NOI THANH CONG qua UART HSU! Chip: PN5%02X v%d.%d | TX=GPIO%d, RX=GPIO%d <<<",
                 (unsigned)((ver>>24)&0xFF), (int)((ver>>16)&0xFF), (int)((ver>>8)&0xFF), tx_pin, rx_pin);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t try_connect_i2c(int scl_pin, int sda_pin) {
    esp_err_t ret = setup_i2c(scl_pin, sda_pin);
    if (ret != ESP_OK) return ret;
    // Wakeup: gửi dummy byte kích hoạt dao động thạch anh PN532 (NACK lần đầu là bình thường)
    uint8_t dummy = 0;
    i2c_master_transmit(s_pn532_dev, &dummy, 1, 50);
    vTaskDelay(pdMS_TO_TICKS(15));
    uint32_t ver = 0;
    for (int i = 0; i < 3; i++) {
        ret = pn532_get_firmware_version(&ver);
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (ret == ESP_OK && ((ver >> 24) & 0xFF) == 0x32) {
        ESP_LOGI(TAG, ">>> KET NOI THANH CONG qua I2C! Chip: PN5%02X v%d.%d | SCL=GPIO%d, SDA=GPIO%d <<<",
                 (unsigned)((ver>>24)&0xFF), (int)((ver>>16)&0xFF), (int)((ver>>8)&0xFF), scl_pin, sda_pin);
        return ESP_OK;
    }
    return ESP_FAIL;
}

// ======================  Scanner task  ===========================

static void pn532_scanner_task(void *arg) {
    ESP_LOGI(TAG, "=== PN532 Universal Scanner Started (GPIO 5 & 6, Auto-detect UART + I2C) ===");

    int step = 0;
    while (s_scanner_active) {
        if (!s_is_connected) {
            esp_err_t conn = ESP_FAIL;
            const char *mode_str = "";
            switch (step % 4) {
                case 0:
                    mode_str = "UART TX=5 RX=6";
                    ESP_LOGI(TAG, "[%d/4] Thu UART HSU (ESP32 TX=GPIO5, RX=GPIO6)...", step%4+1);
                    conn = try_connect_uart(PIN_PN532_GPIO_A, PIN_PN532_GPIO_B);
                    break;
                case 1:
                    mode_str = "UART TX=6 RX=5";
                    ESP_LOGI(TAG, "[%d/4] Thu UART HSU dao chieu (ESP32 TX=GPIO6, RX=GPIO5)...", step%4+1);
                    conn = try_connect_uart(PIN_PN532_GPIO_B, PIN_PN532_GPIO_A);
                    break;
                case 2:
                    mode_str = "I2C SCL=5 SDA=6";
                    ESP_LOGI(TAG, "[%d/4] Thu I2C (SCL=GPIO5, SDA=GPIO6)...", step%4+1);
                    conn = try_connect_i2c(PIN_PN532_GPIO_A, PIN_PN532_GPIO_B);
                    break;
                case 3:
                    mode_str = "I2C SCL=6 SDA=5";
                    ESP_LOGI(TAG, "[%d/4] Thu I2C dao chieu (SCL=GPIO6, SDA=GPIO5)...", step%4+1);
                    conn = try_connect_i2c(PIN_PN532_GPIO_B, PIN_PN532_GPIO_A);
                    break;
            }
            step++;

            if (conn == ESP_OK) {
                pn532_sam_config();
                pn532_set_passive_retries(0x02);
                s_is_connected = true;
                ESP_LOGI(TAG, ">>> PN532 SAN SANG QUET THE NFC! (mode: %s) <<<", mode_str);
                continue;
            }

            // Mỗi vòng 4 bước in 1 lần chẩn đoán
            if (step % 4 == 0) {
                cleanup_all();
                gpio_set_direction((gpio_num_t)PIN_PN532_GPIO_A, GPIO_MODE_INPUT);
                gpio_set_direction((gpio_num_t)PIN_PN532_GPIO_B, GPIO_MODE_INPUT);
                gpio_set_pull_mode((gpio_num_t)PIN_PN532_GPIO_A, GPIO_PULLDOWN_ONLY);
                gpio_set_pull_mode((gpio_num_t)PIN_PN532_GPIO_B, GPIO_PULLDOWN_ONLY);
                vTaskDelay(pdMS_TO_TICKS(15));
                int pd5 = gpio_get_level((gpio_num_t)PIN_PN532_GPIO_A);
                int pd6 = gpio_get_level((gpio_num_t)PIN_PN532_GPIO_B);
                ESP_LOGW(TAG, "=== DA THU HET 4 CHE DO, CHUA TIM THAY PN532 ===");
                ESP_LOGW(TAG, "Pull-down test: GPIO5=%d GPIO6=%d -> %s", pd5, pd6,
                         (pd5||pd6) ? "Co tin hieu (day ket noi)" : "KHONG co tin hieu (day hong?)");
                ESP_LOGW(TAG, "Kiem tra: (1) VCC noi vao 5V (khong phai 3.3V)");
                ESP_LOGW(TAG, "          (2) GND chac chan tiep xuc tot");
                ESP_LOGW(TAG, "          (3) Day SDA/SCL noi vao GPIO5 & GPIO6");
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            continue;
        }

        // Đã kết nối -> Quét thẻ NFC
        uint8_t uid[7] = {0};
        uint8_t uid_len = sizeof(uid);
        esp_err_t err = pn532_read_passive_target(uid, &uid_len);

        if (err == ESP_OK && uid_len > 0) {
            char uid_str[32] = {0};
            for (int i = 0; i < uid_len; i++) {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02X ", uid[i]);
                strcat(uid_str, hex);
            }
            ESP_LOGI(TAG, ">>> THE NFC! UID (%d bytes): %s<<<", uid_len, uid_str);
            if (s_card_callback) s_card_callback(uid, uid_len);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (err != ESP_ERR_NOT_FOUND && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Loi giao tiep (0x%x), dang ket noi lai...", err);
            s_is_connected = false;
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }

    cleanup_all();
    vTaskDelete(NULL);
}

// =======================  Public API  ============================

esp_err_t pn532_driver_init(pn532_card_cb_t card_cb) {
    s_card_callback = card_cb;
    ESP_LOGI(TAG, "Initializing PN532 Universal Driver (Auto-detect UART & I2C on GPIO 5 & 6)...");
    BaseType_t ret = xTaskCreate(pn532_scanner_task, "pn532_scan", 4096, NULL, 3, &s_scanner_task_handle);
    if (ret != pdPASS) { ESP_LOGE(TAG, "Failed to create scanner task!"); return ESP_FAIL; }
    return ESP_OK;
}

bool pn532_is_connected(void) { return s_is_connected; }
void pn532_set_scanner_active(bool active) { s_scanner_active = active; }
