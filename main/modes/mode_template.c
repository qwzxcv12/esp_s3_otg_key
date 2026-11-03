/**
 * @file mode_template.c
 * @brief File mẫu (Template) để tạo chương trình/chế độ mới cho bàn phím đồ chơi.
 * 
 * HƯỚNG DẪN TẠO CHẾ ĐỘ MỚI (Ví dụ Mode 10):
 * 1. Sao chép file này thành mode_xxx.c (ví dụ: mode_racing.c)
 * 2. Thay đổi tên hàm, thông số ID và cấu hình g_mode_xxx_driver ở cuối file.
 * 3. Mở file `mode_registry.c`:
 *    - Thêm: extern const toy_mode_driver_t g_mode_xxx_driver;
 *    - Thêm: mode_manager_register(&g_mode_xxx_driver);
 * 4. Biên dịch và hoàn thành! Không cần đụng đến main.c hay các chế độ khác.
 */

#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"

// Biến trạng thái nội bộ của chế độ mới (dùng static để không đụng độ các file khác)
// static int s_my_score = 0;

/**
 * @brief Khởi tạo một lần khi khởi động ESP32 (Nếu không cần thì để NULL)
 */
static esp_err_t mode_template_init(void) {
    // Khởi tạo biến, bộ nhớ hoặc phần cứng riêng nếu cần
    return ESP_OK;
}

/**
 * @brief Hook khi chuyển VÀO chế độ này
 */
static void mode_template_on_enter(void) {
    printf("\r\n>>> [CHẾ ĐỘ MỚI: TÊN CHẾ ĐỘ SẴN SÀNG] <<<\r\n");
    printf(">>> [Hướng dẫn các phím chức năng của chế độ tại đây...] <<<\r\n\r\n");
}

/**
 * @brief Hook khi chuẩn bị RỜI KHỎI chế độ này
 */
static void mode_template_on_exit(void) {
    // Tắt âm thanh lặp lại, còi hoặc dừng timer nếu có
}

/**
 * @brief Xử lý khi có phím được nhấn
 * @return true nếu phím đã được xử lý
 */
static bool mode_template_handle_key(uint8_t kc, uint8_t mod, char c) {
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // Ví dụ xử lý phím Space
    if (c == ' ') {
        printf(" -> [CHẾ ĐỘ MỚI: Bấm phím SPACE!]\r\n");
        led_effects_color_burst(0, 255, 200);
        return true;
    }

    // Mặc định cho các phím khác
    printf(" -> [CHẾ ĐỘ MỚI: Phím 0x%02X ('%c')]\r\n", kc, c ? c : ' ');
    led_effects_spark();
    return true;
}

/**
 * @brief Xử lý khi phím được nhả ra (Tùy chọn, để NULL nếu không cần)
 */
static void mode_template_handle_key_release(uint8_t kc) {
    // Xử lý khi nhả phím
}

/**
 * @brief Tác vụ nền chạy định kỳ mỗi 50ms (Tùy chọn, để NULL nếu không cần)
 */
static void mode_template_background_tick(uint32_t delta_ms) {
    // Đếm thời gian, chạy animation hoặc kiểm tra timeout...
}

// Cấu hình Driver của chế độ mới
const toy_mode_driver_t g_mode_template_driver = {
    .id                 = 9, // Thay đổi thành ID tiếp theo (ví dụ: 9 cho Mode 10)
    .name               = "TÊN CHƯƠNG TRÌNH MỚI",
    .short_desc         = "Mô tả ngắn gọn chức năng của chương trình mới",
    .init               = mode_template_init,
    .on_enter           = mode_template_on_enter,
    .on_exit            = mode_template_on_exit,
    .handle_key         = mode_template_handle_key,
    .handle_key_release = mode_template_handle_key_release,
    .background_tick    = mode_template_background_tick,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
