#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"

static const char *s_spell_dict[26] = {
    "Chữ A -> A... Con Cá / Quả Cam!",
    "Chữ B (Bờ) -> Quả Bóng / Con Bò!",
    "Chữ C (Cờ) -> Con Cò / Cái Cốc!",
    "Chữ D (Dờ) -> Con Dê / Quả Dưa Hấu!",
    "Chữ E (E) -> Em Bé / Quả Me!",
    "Chữ F (Ép) -> Fansipan / Phích Nước (Frog - Chú Ếch)!",
    "Chữ G (Gờ) -> Con Gà Trống / Cái Ghế!",
    "Chữ H (Hờ) -> Bông Hoa Đẹp / Con Hổ!",
    "Chữ I (I) -> Viên Bi Tròn / Con Khỉ!",
    "Chữ J (Gi) -> Judo / Jaguar (Báo Đốm)!",
    "Chữ K (Ca) -> Cây Kem Mát Lạnh / Cái Kéo!",
    "Chữ L (Lờ) -> Chiếc Lá Xanh / Quả Lê!",
    "Chữ M (Mờ) -> Mặt Trời / Con Mèo Con!",
    "Chữ N (Nờ) -> chiếc Nón Lá / Quả Na!",
    "Chữ O (O) -> Con Ong Chăm Chỉ / Quả Bóng Tròn!",
    "Chữ P (Pờ) -> Đèn Pin Sáng / Phố Phường!",
    "Chữ Q (Quy) -> Quả Quýt Ngọt / Cây Quạt Mát!",
    "Chữ R (Rờ) -> Con Rùa Chậm Chạp / Rừng Cây!",
    "Chữ S (Sờ) -> Chú Sóc Nâu / Ngôi Sao Sáng!",
    "Chữ T (Tờ) -> Đoàn Tàu Hỏa / Quả Táo Đỏ!",
    "Chữ U (U) -> Quả Đu Đủ / Chiếc Mũ Xinh!",
    "Chữ V (Vờ) -> Chú Voi Con / Con Vịt Vàng!",
    "Chữ W (Vê-Kép) -> Wolf (Chó Sói) / Web Toàn Cầu!",
    "Chữ X (Xờ) -> Chiếc Xe Đạp / Quả Xoài Ngọt!",
    "Chữ Y (I-Dài) -> Chim Yến / Yêu Thương Bố Mẹ!",
    "Chữ Z (Dét) -> Zebra (Ngựa Vằn) / Đường Zíc-Zắc!"
};

static void mode_phonics_on_enter(void) {
    printf("\r\n>>> [BÉ HỌC ĐÁNH VẦN CHỮ CÁI & TẬP ĐẾM SỐ SẴN SÀNG] <<<\r\n");
    printf(">>> [Phím A..Z: Đọc tên chữ cái, ngữ âm & từ vựng minh họa] <<<\r\n");
    printf(">>> [Phím 0..9 & Numpad: Bé học đếm số 0..9 | Phím F10 hoặc Shift+0: Bé tập đếm SỐ 10!] <<<\r\n");
    printf(">>> [Phím F1..F9, ESC, Space, Enter, Mũi tên, Dấu câu: Đọc tên từng phím tiếng Việt (Nhấn đúp ESC để đổi chế độ)] <<<\r\n\r\n");
}

static bool mode_phonics_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
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

    // 2. Phím F10 (0x43) hoặc Shift+0 (c == ')'): Bé học đếm SỐ 10 hoàn chỉnh!
    if (kc == 0x43 || (kc == 0x27 && c == ')')) {
        printf(" -> [BÉ HỌC SỐ: SỐ 10 - ĐẾM ĐỦ 10 NHỊP TRỐNG & 10 BÓNG ĐÈN!]\r\n");
        audio_play_counting_number(10);
        return true;
    }

    // 3. Phím số 0..9 trên hàng phím chính hoặc Numpad (Khi không giữ Shift)
    if ((c >= '0' && c <= '9') || (kc >= 0x59 && kc <= 0x62)) {
        uint8_t num = 0;
        if (c >= '0' && c <= '9') num = (uint8_t)(c - '0');
        else if (kc >= 0x59 && kc <= 0x61) num = (uint8_t)(kc - 0x59 + 1); // Numpad 1..9
        else if (kc == 0x62) num = 0; // Numpad 0 = 0

        printf(" -> [BÉ HỌC SỐ: SỐ %d]\r\n", num);
        audio_play_counting_number(num);
        return true;
    }

    // 4. Phím ký tự & dấu câu (Khi bé bấm các dấu +, -, *, /, =, ?, !, @, #...)
    if (c != 0 && c != '\r' && c != '\n' && c != '\b' && c != '\t' && c != ' ' && sb_play_symbol(c)) {
        led_effects_color_burst(100, 220, 255);
        printf(" -> [BÉ HỌC DẤU CÂU & KÝ TỰ: '%c']\r\n", c);
        return true;
    }

    // 5. Phím chức năng & điều hướng đặc biệt (ESC, F1..F9, Space, Enter, Backspace, Tab, Mũi tên, Shift, Ctrl, Alt...)
    if (sb_play_special_key(kc)) {
        led_effects_color_burst(255, 215, 0);
        printf(" -> [BÉ HỌC PHÍM CHỨC NĂNG & ĐIỀU HƯỚNG: 0x%02X]\r\n", kc);
        return true;
    }

    // 6. Khám phá các phím khác
    audio_play_special_key(kc);
    led_effects_spark();
    printf(" -> [BÉ KHÁM PHÁ BÀN PHÍM: 0x%02X]\r\n", kc);
    return true;
}

static void mode_phonics_handle_action(void) {
    audio_play_praise();
    printf("\r\n>>> [BTN 6 ACTION: KHEN NGỢI & VỖ TAY HOAN HÔ!] <<<\r\n");
    fflush(stdout);
    led_effects_celebration();
}

// Cấu hình Driver cho Chế độ 9
const toy_mode_driver_t g_mode_phonics_driver = {
    .id                 = 8,
    .name               = "9. BÉ HỌC ĐÁNH VẦN CHỮ CÁI & TẬP ĐẾM SỐ (PHONICS & COUNTING)",
    .short_desc         = "Phát âm chuẩn tiếng Việt bảng chữ cái 26 chữ, tập đếm 0-10 và khám phá mọi phím chức năng",
    .init               = NULL,
    .on_enter           = mode_phonics_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_phonics_handle_key,
    .handle_key_release = NULL,
    .background_tick    = NULL,
    .handle_action      = mode_phonics_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
