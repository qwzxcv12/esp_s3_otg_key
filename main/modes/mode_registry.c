#include "mode_manager.h"

// Khai báo các Driver chế độ chuẩn hóa
extern const toy_mode_driver_t g_mode_piano_driver;       // Mode 1: Đàn Piano Diệu Kỳ & Ngũ Cung
extern const toy_mode_driver_t g_mode_mozart_driver;      // Mode 2: Nghệ Sĩ Nhí Baby Mozart
extern const toy_mode_driver_t g_mode_speed_driver;       // Mode 3: Nhạc Chạy Theo Tốc Độ Gõ Phím
extern const toy_mode_driver_t g_mode_drum_driver;        // Mode 4: Dàn Trống Beat-Maker & DJ
extern const toy_mode_driver_t g_mode_typewriter_driver;  // Mode 5: Máy Gõ Chữ Cổ Điển
extern const toy_mode_driver_t g_mode_cartoon_driver;     // Mode 6: Hộp Âm Thanh Hoạt Hình & Game SFX
extern const toy_mode_driver_t g_mode_agent_driver;       // Mode 7: Điệp Viên Nhí & Robot R2-D2
extern const toy_mode_driver_t g_mode_cockpit_driver;     // Mode 8: Buồng Lái Xe Cứu Hỏa & 26 Con Thú
extern const toy_mode_driver_t g_mode_phonics_driver;     // Mode 9: Bé Học Đánh Vần & Tập Đếm Số

/**
 * @brief Đăng ký danh sách toàn bộ các chế độ vào hệ thống.
 * Khi cần bổ sung chế độ mới (ví dụ Mode 10):
 * Chỉ cần extern con trỏ driver mới và thêm 1 dòng mode_manager_register(...) tại đây!
 */
void mode_registry_register_all(void) {
    mode_manager_register(&g_mode_piano_driver);
    mode_manager_register(&g_mode_mozart_driver);
    mode_manager_register(&g_mode_speed_driver);
    mode_manager_register(&g_mode_drum_driver);
    mode_manager_register(&g_mode_typewriter_driver);
    mode_manager_register(&g_mode_cartoon_driver);
    mode_manager_register(&g_mode_agent_driver);
    mode_manager_register(&g_mode_cockpit_driver);
    mode_manager_register(&g_mode_phonics_driver);
}
