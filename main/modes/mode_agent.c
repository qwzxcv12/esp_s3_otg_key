#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

// Bảng tra cứu mã Morse nội bộ (Đóng gói an toàn)
static const char *s_morse_letters[26] = {
    ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....", "..",
    ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.", "--.-", ".-.",
    "...",  "-",    "..-",  "...-", ".--",  "-..-", "-.--", "--.."
};

static const char *s_morse_digits[10] = {
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
};

static void mode_agent_on_enter(void) {
    printf("\r\n>>> [ĐIỆP VIÊN NHÍ & ROBOT R2-D2 TRÊN KHUNG HÌNH VUÔNG 32 LED] <<<\r\n");
    printf(">>> [Chữ A-Z & Số 0-9: Truyền Quang Học Mã Morse Quanh 4 Cạnh | Space: Radar Xoay 360° | Phím khác: Bảng Mạch R2-D2] <<<\r\n\r\n");
    led_effects_radar_sweep();
}

static bool mode_agent_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    if (upper >= 'A' && upper <= 'Z') {
        const char *morse = s_morse_letters[upper - 'A'];
        audio_play_morse_char(c);
        led_effects_morse_code(morse);
        printf(" -> [ĐIỆP VIÊN: MÃ MORSE QUANG HỌC '%c' = %s (CHẠY QUANH 4 CẠNH)]\r\n", upper, morse);
    } else if (c >= '0' && c <= '9') {
        const char *morse = s_morse_digits[c - '0'];
        audio_play_morse_char(c);
        led_effects_morse_code(morse);
        printf(" -> [ĐIỆP VIÊN: MÃ MORSE SỐ '%c' = %s (CHẠY QUANH 4 CẠNH)]\r\n", c, morse);
    } else if (c == ' ' || kc == 0x2C) {
        // Phím Space: Tia Radar Sonar xoay 360° quanh 4 cạnh khung vuông + nháy đỏ mục tiêu
        audio_play_piano_note_dur(1046.50f, TIMBRE_MUSIC_BOX, 0, 180);
        audio_play_robot_chirp();
        led_effects_radar_sweep();
        printf(" -> [ĐIỆP VIÊN: QUÉT SÓNG RADAR 360 ĐỘ QUANH KHUNG VUÔNG... PHÁT HIỆN MỤC TIÊU!]\r\n");
    } else if (c == '\r' || c == '\n' || kc == 0x28 || kc == 0x58) {
        // Phím Enter: Giải mã mật thư thành công!
        audio_play_praise();
        led_effects_celebration();
        printf(" -> [ĐIỆP VIÊN: GIẢI MÃ MẬT THƯ THÀNH CÔNG - NHIỆM VỤ HOÀN TẤT!]\r\n");
    } else if (kc >= 0x4F && kc <= 0x52) {
        // 4 Phím Mũi Tên: Quét kiểm tra an ninh từng Cạnh Hình Vuông (Trên / Phải / Dưới / Trái)
        uint8_t side = (kc == 0x52) ? 0 : (kc == 0x4F) ? 1 : (kc == 0x51) ? 2 : 3;
        audio_play_robot_chirp();
        led_effects_square_side_pulse(side, 0, 255, 100);
        printf(" -> [ĐIỆP VIÊN: QUÉT AN NINH CẠNH %u HÌNH VUÔNG!]\r\n", (unsigned int)side);
    } else {
        // Bảng mạch Holo-Display 8 ô của Robot R2-D2
        audio_play_robot_chirp();
        led_effects_r2d2_holo();
        printf(" -> [ROBOT R2-D2: BẢNG MẠCH HOLO-DISPLAY 4 CẠNH BÍP BOOP CHIRP!]\r\n");
    }
    return true;
}

static void mode_agent_handle_action(void) {
    audio_play_robot_chirp();
    printf("\r\n>>> [BTN 6 ACTION: ROBOT R2-D2 LÍU LO & QUÉT RADAR 360°] <<<\r\n");
    fflush(stdout);
    led_effects_radar_sweep();
}

const toy_mode_driver_t g_mode_agent_driver = {
    .id                 = 6,
    .name               = "7. ĐIỆP VIÊN NHÍ & ROBOT R2-D2",
    .short_desc         = "Truyền mã Morse quang học quanh 4 cạnh, Radar xoay 360° và bảng mạch R2-D2",
    .init               = NULL,
    .on_enter           = mode_agent_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_agent_handle_key,
    .handle_key_release = NULL,
    .background_tick    = NULL,
    .handle_action      = mode_agent_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
