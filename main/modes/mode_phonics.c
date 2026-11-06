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
    printf("\r\n>>> [BÉ HỌC ĐÁNH VẦN CHỮ CÁI & TẬP ĐẾM SỐ TRÊN KHUNG VUÔNG 32 LED] <<<\r\n");
    printf(">>> [Phím A..Z: Bản đồ chữ cái cầu vồng quanh 4 cạnh | Phím 0..10: Đoàn tàu đếm số cầu vồng] <<<\r\n");
    printf(">>> [4 Phím Mũi Tên: Học 4 hướng (Lên/Phải/Xuống/Trái) sáng đúng 4 cạnh hình vuông!] <<<\r\n\r\n");
}

static bool mode_phonics_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 1. Phím chữ cái A - Z: Phát âm ngữ âm tiếng Việt & thắp sáng vị trí chữ cái trên khung vuông
    if (upper >= 'A' && upper <= 'Z') {
        audio_play_phonics_letter(upper);
        led_effects_phonics_letter(upper);

        const char *spell_text = s_spell_dict[upper - 'A'];
        printf(" -> [BÉ HỌC ĐÁNH VẦN: %s]\r\n", spell_text);
        return true;
    }

    // 2. Phím F10 (0x43) hoặc Shift+0 (c == ')'): Bé học đếm SỐ 10 hoàn chỉnh!
    if (kc == 0x43 || (kc == 0x27 && c == ')')) {
        printf(" -> [BÉ HỌC SỐ: SỐ 10 - ĐẾM ĐỦ 10 NHỊP TRỐNG & 10 BÓNG ĐÈN CẦU VỒNG!]\r\n");
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

    // 4. 4 Phím Mũi Tên: Học phương hướng trực quan trên 4 Cạnh Hình Vuông (Trên / Phải / Dưới / Trái)
    if (kc >= 0x4F && kc <= 0x52) {
        uint8_t side = (kc == 0x52) ? 0 : (kc == 0x4F) ? 1 : (kc == 0x51) ? 2 : 3;
        if (!sb_play_special_key(kc)) {
            audio_play_special_key(kc);
        }
        led_effects_square_side_pulse(side, 0, 220, 255);
        printf(" -> [BÉ HỌC HƯỚNG: THẮP SÁNG CẠNH %u HÌNH VUÔNG (0x%02X)]\r\n", (unsigned int)side, kc);
        return true;
    }

    // 5. Phím ký tự & dấu câu (+, -, *, /, =, ?, !, @, #...)
    if (c != 0 && c != '\r' && c != '\n' && c != '\b' && c != '\t' && c != ' ' && sb_play_symbol(c)) {
        led_effects_piano_key((c >> 3) & 3, c & 7, 2, 80, 220, 255);
        printf(" -> [BÉ HỌC DẤU CÂU & KÝ TỰ: '%c']\r\n", c);
        return true;
    }

    // 6. Phím chức năng đặc biệt (ESC, F1..F9, Space, Enter, Backspace, Tab, Shift, Ctrl, Alt...)
    if (sb_play_special_key(kc)) {
        led_effects_piano_key((kc >> 3) & 3, kc & 7, 0, 255, 215, 0);
        printf(" -> [BÉ HỌC PHÍM CHỨC NĂNG: 0x%02X]\r\n", kc);
        return true;
    }

    // 7. Khám phá các phím khác
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

const toy_mode_driver_t g_mode_phonics_driver = {
    .id                 = 8,
    .name               = "9. BÉ HỌC ĐÁNH VẦN CHỮ CÁI & TẬP ĐẾM SỐ (PHONICS & COUNTING)",
    .short_desc         = "Bản đồ chữ cái cầu vồng 4 cạnh, tập đếm 0-10 và học 4 hướng trên khung vuông",
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
