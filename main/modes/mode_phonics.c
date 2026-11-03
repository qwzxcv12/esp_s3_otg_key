#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"

static const char *s_spell_dict[26] = {
    "Chữ A -> A... Quả Táo / Con Gà!",
    "Chữ B (Bờ) -> Quả Bóng / Con Bò!",
    "Chữ C (Cờ) -> Con Cá / Cái Cốc!",
    "Chữ D (Dờ) -> Con Dê / Quả Dưa Hấu!",
    "Chữ E (E) -> Em Bé / Quả Me!",
    "Chữ F (Ép) -> Pháo Hoa Rực Rỡ!",
    "Chữ G (Gờ) -> Con Gà Trống / Cái Ghế!",
    "Chữ H (Hờ) -> Bông Hoa Đẹp / Con Hổ!",
    "Chữ I (I) -> Viên Bi Tròn / Trái Mận!",
    "Chữ J (Gi) -> Chiếc Hộp Nhạc Vui Nhộn!",
    "Chữ K (Ca) -> Cây Kem Mát Lạnh / Cái Kéo!",
    "Chữ L (Lờ) -> Chiếc Lá Xanh / Con Lợn Ỉn!",
    "Chữ M (Mờ) -> Mặt Trời Chiếu Sáng / Con Mèo Con!",
    "Chữ N (Nờ) -> Ngôi Sao Sáng / Quả Na!",
    "Chữ O (O) -> Quả Trứng Gà Tròn Xoe / Con Ong!",
    "Chữ P (Pờ) -> Cây Cầu Vồng / Đèn Pin Sáng!",
    "Chữ Q (Quy) -> Con Quạ Đen / Cây Quạt Mát!",
    "Chữ R (Rờ) -> Con Rùa Chậm Chạp / Rừng Cây!",
    "Chữ S (Sờ) -> Chú Sóc Nâu Nhặt Hạt / Dòng Sông!",
    "Chữ T (Tờ) -> Đoàn Tàu Hỏa Xình Xịch / Con Tôm!",
    "Chữ U (U) -> Đám Mây Trắng / Cầu Vồng 7 Màu!",
    "Chữ V (Vờ) -> Chú Voi Con Vẫy Vòi / Con Vịt Vàng!",
    "Chữ W (Vê-Kép) -> Thế Giới Kỳ Thú / Bánh Xe!",
    "Chữ X (Xờ) -> Chiếc Xe Ô Tô Chạy Nhanh / Quả Xoài Ngọt!",
    "Chữ Y (I-Dài) -> Yêu Thương Bố Mẹ / Cây Tre Xanh!",
    "Chữ Z (Dét) -> Chú Ngựa Vằn Zebra / Đám Mây Sấm Sét!"
};

static void mode_phonics_on_enter(void) {
    printf("\r\n>>> [BÉ HỌC ĐÁNH VẦN CHỮ CÁI & TẬP ĐẾM SỐ SẴN SÀNG] <<<\r\n");
    printf(">>> [Phím A..Z: Đọc tên chữ cái, ngữ âm & từ vựng minh họa] <<<\r\n");
    printf(">>> [Phím 0..9 & Numpad: Bé học đếm số với giọng đọc chuẩn] <<<\r\n");
    printf(">>> [Phím ký tự & điều hướng: Khám phá các nút chức năng trên bàn phím] <<<\r\n\r\n");
}

static bool mode_phonics_handle_key(uint8_t kc, uint8_t mod, char c) {
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 1. Phím chữ cái A - Z: Phát âm ngữ âm tiếng Việt & từ vựng
    if (upper >= 'A' && upper <= 'Z') {
        audio_play_phonics_letter(upper);

        // Hiệu ứng màu riêng biệt cho từng chữ cái
        uint8_t r = 50 + ((upper * 37) % 205);
        uint8_t g = 50 + ((upper * 67) % 205);
        uint8_t b = 50 + ((upper * 97) % 205);
        led_effects_color_burst(r, g, b);

        const char *spell_text = s_spell_dict[upper - 'A'];
        printf(" -> [BÉ HỌC ĐÁNH VẦN: %s]\r\n", spell_text);
        return true;
    }
    // 2. Phím số 0..9 trên hàng phím chính hoặc Numpad
    else if ((c >= '0' && c <= '9') || (kc >= 0x59 && kc <= 0x62)) {
        uint8_t num = 0;
        if (c >= '0' && c <= '9') num = c - '0';
        else if (kc >= 0x59 && kc <= 0x61) num = kc - 0x59 + 1; // Numpad 1..9
        else if (kc == 0x62) num = 0; // Numpad 0 = 0

        printf(" -> [BÉ HỌC SỐ: SỐ %d]\r\n", num);
        audio_play_counting_number(num);
        return true;
    }
    // 3. Phím ký tự & dấu câu
    else if (c != 0 && sb_play_symbol(c)) {
        led_effects_color_burst(100, 220, 255);
        printf(" -> [BÉ HỌC DẤU CÂU & KÝ TỰ: '%c']\r\n", c);
        return true;
    }
    // 4. Phím chức năng & điều hướng đặc biệt
    else if (sb_play_special_key(kc)) {
        led_effects_color_burst(255, 215, 0);
        printf(" -> [BÉ HỌC PHÍM CHỨC NĂNG & ĐIỀU HƯỚNG: 0x%02X]\r\n", kc);
        return true;
    }
    // 5. Khám phá các phím khác
    else {
        audio_play_special_key(kc);
        led_effects_spark();
        printf(" -> [BÉ KHÁM PHÁ BÀN PHÍM: 0x%02X]\r\n", kc);
        return true;
    }
}

// Cấu hình Driver cho Chế độ 9
const toy_mode_driver_t g_mode_phonics_driver = {
    .id                 = 8,
    .name               = "9. BÉ HỌC ĐÁNH VẦN CHỮ CÁI & TẬP ĐẾM SỐ (PHONICS & COUNTING)",
    .short_desc         = "Phát âm chuẩn tiếng Việt bảng chữ cái 26 chữ, tập đếm 0-9 và khám phá ký hiệu bàn phím",
    .init               = NULL,
    .on_enter           = mode_phonics_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_phonics_handle_key,
    .handle_key_release = NULL,
    .background_tick    = NULL,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
