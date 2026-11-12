#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "mode_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_TOY_MODES 16

/**
 * @brief Khởi tạo bộ quản lý chế độ (Mode Manager).
 * Đăng ký toàn bộ danh sách chế độ từ bảng Registry.
 */
esp_err_t mode_manager_init(void);

/**
 * @brief Đăng ký một chế độ mới vào danh sách.
 * @param driver Con trỏ tới cấu trúc toy_mode_driver_t
 */
esp_err_t mode_manager_register(const toy_mode_driver_t *driver);

/**
 * @brief Chuyển sang một chế độ cụ thể theo chỉ mục (0, 1, 2...)
 * @param mode_index Chỉ mục chế độ
 */
void mode_manager_switch_to(uint8_t mode_index);

/**
 * @brief Chuyển sang một chế độ ngẫu nhiên khác chế độ hiện tại.
 */
void mode_manager_random(void);

/**
 * @brief Kiểm tra xem hệ thống có đang ở trạng thái chờ khởi động (Standby) không.
 */
bool mode_manager_is_in_standby(void);

/**
 * @brief Đưa hệ thống về trạng thái chờ khởi động (Standby).
 */
void mode_manager_enter_standby(void);

/**
 * @brief Chuyển sang chế độ tiếp theo (vòng tròn).
 */
void mode_manager_next(void);

/**
 * @brief Chuyển sang chế độ trước đó (vòng tròn).
 */
void mode_manager_prev(void);

/**
 * @brief Điều phối sự kiện phím nhấn tới chế độ hiện tại.
 */
bool mode_manager_dispatch_key(uint8_t keycode, uint8_t modifier, char ascii_char);

/**
 * @brief Điều phối sự kiện nút ACTION / SONG (BTN 6) tới chế độ hiện tại.
 */
void mode_manager_dispatch_action(void);

/**
 * @brief Điều phối sự kiện nhả phím tới chế độ hiện tại.
 */
void mode_manager_dispatch_release(uint8_t keycode);

/**
 * @brief Gọi tác vụ nền của chế độ hiện tại định kỳ.
 * @param delta_ms Chu kỳ gọi (ms)
 */
void mode_manager_tick(uint32_t delta_ms);

/**
 * @brief Lấy con trỏ chế độ hiện tại.
 */
const toy_mode_driver_t *mode_manager_get_current(void);

/**
 * @brief Lấy chỉ mục chế độ hiện tại (0..N-1).
 */
uint8_t mode_manager_get_current_index(void);

/**
 * @brief Lấy tổng số lượng chế độ đã đăng ký.
 */
uint8_t mode_manager_get_count(void);

/**
 * @brief Lấy driver của chế độ theo chỉ mục.
 */
const toy_mode_driver_t *mode_manager_get_driver(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif // MODE_MANAGER_H
