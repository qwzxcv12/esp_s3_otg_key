#ifndef MODE_DEMO_H
#define MODE_DEMO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo module biểu diễn Demo 30s đặc sắc.
 */
void mode_demo_init(void);

/**
 * @brief Bắt đầu bản demo 30s đặc sắc cho chế độ mode_index (0..9).
 * Bản demo sẽ tự động chạy trong 30 giây (hoặc dừng ngay khi bé chạm phím).
 */
void mode_demo_start(uint8_t mode_index);

/**
 * @brief Dừng ngay lập tức bản demo đang chạy (khi bé gõ phím hoặc chuyển chế độ).
 */
void mode_demo_stop(void);

/**
 * @brief Kiểm tra xem bản demo có đang phát hay không.
 */
bool mode_demo_is_active(void);

/**
 * @brief Cập nhật sequencer của bản demo (gọi từ mode_manager_tick mỗi 50ms).
 */
void mode_demo_tick(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif // MODE_DEMO_H
