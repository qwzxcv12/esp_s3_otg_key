#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Các trạng thái năng lượng của hệ thống
 */
typedef enum {
    POWER_STATE_ACTIVE = 0,    // Hoạt động bình thường: 100% độ sáng, âm thanh đầy đủ
    POWER_STATE_DIMMED,        // Giảm sáng tiết kiệm nhẹ: độ sáng ~20%, giảm tải
    POWER_STATE_PRE_SLEEP,     // Chuẩn bị ngủ: phát âm thanh tạm biệt du dương
    POWER_STATE_SLEEP          // Ngủ sâu tối đa: tắt LED, ngắt I2S amp, tắt đèn bàn phím
} power_state_t;

// Cấu hình thời gian rảnh (Inactivity Timeouts)
#define POWER_INACTIVITY_DIM_MS     60000   // 60 giây không thao tác -> Giảm độ sáng LED
#define POWER_INACTIVITY_SLEEP_MS   180000  // 180 giây (3 phút) không thao tác -> Đi ngủ

/**
 * @brief Callback thông báo các sự kiện quản lý năng lượng
 */
typedef struct {
    void (*on_state_change)(power_state_t old_state, power_state_t new_state);
    void (*on_pre_sleep)(void);     // Được gọi khi bắt đầu vào PRE_SLEEP (phát âm tạm biệt)
    void (*on_enter_sleep)(void);   // Được gọi khi chính thức vào SLEEP (tắt LED, ngắt clock)
    void (*on_wake_up)(void);       // Được gọi khi vừa thức dậy (khôi phục ngoại vi)
    bool (*can_sleep)(void);        // Kiểm tra xem hệ thống có được phép tự động vào chế độ ngủ không
} power_manager_cbs_t;

/**
 * @brief Khởi tạo hệ thống quản lý năng lượng thông minh
 */
esp_err_t power_manager_init(const power_manager_cbs_t *callbacks);

/**
 * @brief Báo hiệu có tương tác từ người dùng (bất kỳ phím bàn phím hoặc nút bấm nào)
 * @return true nếu hệ thống vừa được đánh thức từ trạng thái SLEEP hoặc PRE_SLEEP
 */
bool power_manager_feed_activity(void);

/**
 * @brief Cập nhật định kỳ bộ đếm thời gian và chuyển trạng thái (gọi trong task nền 25ms)
 * @param delta_ms Chu kỳ gọi (ms)
 */
void power_manager_tick(uint32_t delta_ms);

/**
 * @brief Lấy trạng thái năng lượng hiện tại
 */
power_state_t power_manager_get_state(void);

/**
 * @brief Lấy tên mô tả trạng thái hiện tại
 */
const char *power_manager_get_state_name(power_state_t state);

/**
 * @brief Kiểm tra xem hệ thống có đang ngủ không (SLEEP hoặc PRE_SLEEP)
 */
bool power_manager_is_sleeping(void);

/**
 * @brief Kiểm tra xem hệ thống có đang ở trạng thái giảm độ sáng không
 */
bool power_manager_is_dimmed(void);

/**
 * @brief Lấy giá trị độ sáng hiện tại (0..255)
 */
uint8_t power_manager_get_brightness(void);

/**
 * @brief Chủ động đưa máy vào chế độ ngủ ngay lập tức (phát âm tạm biệt rồi ngủ)
 */
void power_manager_force_sleep(void);

/**
 * @brief Đánh thức cưỡng bức ngay lập tức
 */
void power_manager_force_wake(void);

#ifdef __cplusplus
}
#endif

#endif // POWER_MANAGER_H
