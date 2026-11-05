#ifndef MODE_INTERFACE_H
#define MODE_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Chuẩn giao tiếp cho mỗi chế độ/chương trình bàn phím (Mode Driver Interface).
 * Mọi chế độ (Mode 1 đến Mode 9 và các chế độ tương lai) đều tuân theo cấu trúc này.
 */
typedef struct toy_mode_driver {
    uint8_t     id;                  ///< ID định danh của chế độ (0, 1, 2...)
    const char *name;                ///< Tên hiển thị đầy đủ (Banner)
    const char *short_desc;          ///< Mô tả ngắn / tóm tắt chức năng phím

    // --- Vòng đời của chế độ (Lifecycle Callbacks) ---
    esp_err_t (*init)(void);         ///< Gọi một lần khi ESP32 khởi động (nếu có tài nguyên cần setup)
    void      (*on_enter)(void);     ///< Gọi khi người dùng chuyển VÀO chế độ này
    void      (*on_exit)(void);      ///< Gọi khi người dùng RỜI KHỎI chế độ này (dọn dẹp còi, loop...)

    // --- Xử lý sự kiện bàn phím (Key Events) ---
    /**
     * @brief Xử lý khi một phím được nhấn (KEY_PRESSED).
     * @param keycode USB HID Keycode (0x04 - 0xE7)
     * @param modifier Trạng thái các phím Ctrl/Alt/Shift/Win
     * @param ascii_char Ký tự ASCII (nếu có, 'a'-'z', '0'-'9', hoặc 0 nếu là phím điều hướng/F-keys)
     * @return true nếu phím đã được xử lý, false nếu không dùng đến
     */
    bool      (*handle_key)(uint8_t keycode, uint8_t modifier, char ascii_char);

    /**
     * @brief Xử lý khi phím được nhả ra (KEY_RELEASED).
     * Dùng cho các chế độ cần nhả âm thanh tự nhiên như phím piano, chân ga...
     */
    void      (*handle_key_release)(uint8_t keycode);

    // --- Tác vụ chạy nền định kỳ (Background Tick) ---
    /**
     * @brief Gọi định kỳ (mỗi 50ms) bởi toy_background_task trong FreeRTOS.
     * @param delta_ms Khoảng thời gian từ lần gọi trước (ms)
     */
    void      (*background_tick)(uint32_t delta_ms);

    // --- Các hook mở rộng cho phần cứng tương lai ---
    void      (*handle_action)(void);            ///< Xử lý nút ACTION / SONG (BTN 6) riêng cho từng chế độ
    void      (*handle_encoder)(int delta);      ///< Xử lý núm xoay riêng biệt cho từng chế độ (nếu có)
    void      (*draw_screen)(void *disp_ctx);    ///< Dành cho màn hình LCD/OLED mở rộng
} toy_mode_driver_t;

#ifdef __cplusplus
}
#endif

#endif // MODE_INTERFACE_H
