#ifndef PN532_DRIVER_H
#define PN532_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Chân I2C kết nối PN532 (GPIO 5 = SCL, GPIO 6 = SDA)
#define PIN_PN532_SDA    6   // GPIO 6 nối vào chân SDA của PN532
#define PIN_PN532_SCL    5   // GPIO 5 nối vào chân SCL của PN532
#define PN532_I2C_ADDR   0x24

typedef void (*pn532_card_cb_t)(const uint8_t *uid, uint8_t uid_len);

/**
 * @brief Khởi tạo driver PN532 qua giao tiếp I2C (SDA=6, SCL=5).
 * Tự động kiểm tra Firmware, cấu hình SAM, đặt MaxRetries và chạy Scanner Task ngầm.
 * @param card_cb Hàm callback khi phát hiện thẻ RFID/NFC mới
 * @return ESP_OK nếu thành công
 */
esp_err_t pn532_driver_init(pn532_card_cb_t card_cb);

/**
 * @brief Kiểm tra xem module PN532 có đang kết nối và phản hồi tốt không.
 */
bool pn532_is_connected(void);

/**
 * @brief Bật / Tắt tiến trình quét thẻ ngầm (ví dụ khi vào chế độ ngủ Sleep).
 */
void pn532_set_scanner_active(bool active);

#ifdef __cplusplus
}
#endif

#endif // PN532_DRIVER_H
