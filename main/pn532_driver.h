#ifndef PN532_DRIVER_H
#define PN532_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Cấu hình chân UART HSU (High-Speed UART) cho PN532
#define PIN_PN532_RX       5   // GPIO 5 (ESP32 RX) nối vào chân TX / TXD của PN532
#define PIN_PN532_TX       6   // GPIO 6 (ESP32 TX) nối vào chân RX / RXD của PN532
#define PN532_UART_PORT    UART_NUM_1
#define PN532_UART_BAUD    115200

typedef void (*pn532_card_cb_t)(const uint8_t *uid, uint8_t uid_len);

/**
 * @brief Khởi tạo driver PN532 qua giao tiếp UART HSU (TX=GPIO 6, RX=GPIO 5).
 * Không cần bất kỳ điện trở kéo nào.
 * Tự động gửi Wakeup chuỗi xung, kiểm tra Firmware, cấu hình SAM và chạy task quét thẻ.
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
