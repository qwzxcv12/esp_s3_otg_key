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
    printf("\r\n>>> [ĐIỆP VIÊN NHÍ & ROBOT R2-D2 SẴN SÀNG] <<<\r\n");
    printf(">>> [Chữ cái A-Z và Số 0-9: Mã Morse đa âm tần | Space: Sóng Radar | Enter: Giải Mã Thành Công | Phím khác: Robot R2-D2] <<<\r\n\r\n");
    led_effects_matrix();
}

static bool mode_agent_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    if (upper >= 'A' && upper <= 'Z') {
        audio_play_morse_char(c);
        led_effects_matrix();
        printf(" -> [ĐIỆP VIÊN: MÃ MORSE '%c' = %s (TÍT TE ĐA ÂM TẦN)]\r\n", upper, s_morse_letters[upper - 'A']);
    } else if (c >= '0' && c <= '9') {
        audio_play_morse_char(c);
        led_effects_matrix();
        printf(" -> [ĐIỆP VIÊN: MÃ MORSE SỐ '%c' = %s]\r\n", c, s_morse_digits[c - '0']);
    } else if (c == ' ' || kc == 0x2C) {
        // Phím Space: Phát sóng Radar Sonar Ping dò tìm mục tiêu
        audio_play_piano_note_dur(1046.50f, TIMBRE_MUSIC_BOX, 0, 180);
        audio_play_robot_chirp();
        led_effects_color_burst(0, 255, 120);
        printf(" -> [ĐIỆP VIÊN: QUÉT SÓNG RADAR SONAR PING... BÍP!]\r\n");
    } else if (c == '\r' || c == '\n' || kc == 0x28 || kc == 0x58) {
        // Phím Enter: Giải mã mật thư thành công!
        audio_play_praise();
        led_effects_celebration();
        printf(" -> [ĐIỆP VIÊN: GIẢI MÃ MẬT THƯ THÀNH CÔNG - NHIỆM VỤ HOÀN TẤT!]\r\n");
    } else { // Phím mũi tên, Numpad, F-keys, Symbols...
        audio_play_robot_chirp();
        led_effects_matrix();
        printf(" -> [ROBOT R2-D2: LÍU LO BÍP BOOP CHIRP!]\r\n");
    }
    return true;
}

// Cấu hình Driver cho Chế độ 7
const toy_mode_driver_t g_mode_agent_driver = {
    .id                 = 6,
    .name               = "7. ĐIỆP VIÊN NHÍ & ROBOT R2-D2",
    .short_desc         = "Phát tín hiệu mật mã Morse đa âm tần, sóng Radar và giọng líu lo của robot R2-D2",
    .init               = NULL,
    .on_enter           = mode_agent_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_agent_handle_key,
    .handle_key_release = NULL,
    .background_tick    = NULL,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
