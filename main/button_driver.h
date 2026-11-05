#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CẤU HÌNH CHÂN GPIO CHO 6 NÚT NHẤN (ESP32-S3 N16R8)
// Đấu nối: 1 chân nút nối GPIO, 1 chân nối GND (Active-Low, bật Pull-up nội bộ)
// ============================================================================
#define PIN_BTN_VOL_DOWN     41  // BTN 1: Giảm âm lượng (GPIO 41)
#define PIN_BTN_VOL_UP       47  // BTN 2: Tăng âm lượng (GPIO 47)
#define PIN_BTN_MODE         48  // BTN 3: Chuyển chế độ (GPIO 48)
#define PIN_BTN_LED_MODE     21  // BTN 4: Đổi hiệu ứng đèn LED (GPIO 21)
#define PIN_BTN_MUTE_OFF     15  // BTN 5: Tắt tiếng / Bật lại (GPIO 15)
#define PIN_BTN_ACTION_SONG  42  // BTN 6: Đổi bài hát / Tác vụ đặc biệt (GPIO 42)

typedef enum {
    BUTTON_VOL_DOWN = 0,
    BUTTON_VOL_UP,
    BUTTON_MODE,
    BUTTON_LED_MODE,
    BUTTON_MUTE_OFF,
    BUTTON_ACTION_SONG,
    BUTTON_TOTAL_COUNT
} button_id_t;

typedef enum {
    BUTTON_CLICK = 0,      // Nhấn nhả thông thường (< 800ms)
    BUTTON_LONG_PRESS,     // Nhấn giữ lâu (>= 1000ms)
    BUTTON_REPEAT          // Tự động lặp lại khi giữ (dành cho tăng/giảm âm lượng)
} button_press_type_t;

/**
 * @brief Callback nhận sự kiện nút nhấn
 */
typedef void (*button_event_cb_t)(button_id_t btn, button_press_type_t type);

/**
 * @brief Khởi tạo driver 6 nút nhấn với chống rung phần mềm và FreeRTOS task
 * @param callback Hàm callback khi có sự kiện nút bấm
 */
esp_err_t button_driver_init(button_event_cb_t callback);

/**
 * @brief Lấy tên mô tả của nút theo ID
 */
const char *button_driver_get_name(button_id_t btn);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_DRIVER_H
