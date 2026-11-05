#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

// Biến trạng thái nội bộ của Mode 1 (Đóng gói an toàn)
static piano_timbre_t s_piano_timbre = TIMBRE_PIANO;
static bool s_pentatonic_mode = false;

// Bảng 20 nốt Ngũ Cung Đô Trưởng (C - D - E - G - A) trải dài từ C3 -> A6
static const float s_pentatonic_freqs[20] = {
    130.81f, 146.83f, 164.81f, 196.00f, 220.00f, // C3, D3, E3, G3, A3 (0..4)
    261.63f, 293.66f, 329.63f, 392.00f, 440.00f, // C4, D4, E4, G4, A4 (5..9)
    523.25f, 587.33f, 659.25f, 783.99f, 880.00f, // C5, D5, E5, G5, A5 (10..14)
    1046.50f, 1174.66f, 1318.51f, 1567.98f, 1760.00f // C6, D6, E6, G6, A6 (15..19)
};

static const char *s_pentatonic_names[20] = {
    "Đồ Trầm (C3 Ngũ Cung)", "Rê Trầm (D3 Ngũ Cung)", "Mi Trầm (E3 Ngũ Cung)", "Son Trầm (G3 Ngũ Cung)", "La Trầm (A3 Ngũ Cung)",
    "Đồ (C4 Ngũ Cung)",      "Rê (D4 Ngũ Cung)",      "Mi (E4 Ngũ Cung)",      "Son (G4 Ngũ Cung)",      "La (A4 Ngũ Cung)",
    "Đố (C5 Ngũ Cung)",      "Rế (D5 Ngũ Cung)",      "Mí (E5 Ngũ Cung)",      "Són (G5 Ngũ Cung)",      "Lá (A5 Ngũ Cung)",
    "Đố Cao (C6 Ngũ Cung)",  "Rế Cao (D6 Ngũ Cung)",  "Mí Cao (E6 Ngũ Cung)",  "Són Cao (G6 Ngũ Cung)",  "Lá Cao (A6 Ngũ Cung)"
};

static void mode_piano_on_enter(void) {
    printf("\r\n>>> [CHẾ ĐỘ ĐÀN PIANO DIỆU KỲ & NGŨ CUNG SẴN SÀNG] <<<\r\n");
    printf(">>> [Phím TAB: Bật/Tắt thang âm ngũ cung (Chống phô toàn bàn phím) | Phím F10: Đổi âm sắc Piano/8-bit/Music Box] <<<\r\n\r\n");
}

static bool mode_piano_handle_key(uint8_t kc, uint8_t mod, char c) {
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 1. Phím TAB: Bật / tắt thang âm ngũ cung trên toàn bàn phím
    if (kc == 0x2B) {
        s_pentatonic_mode = !s_pentatonic_mode;
        printf(" -> [THANG ÂM NGŨ CUNG (PENTATONIC): %s]\r\n", s_pentatonic_mode ? "BẬT (CHỐNG PHÔ TOÀN BÀN PHÍM)" : "TẮT (CHUẨN 7 NỐT & PHÍM ĐEN)");
        audio_play_piano_note_dur(523.25f, s_piano_timbre, 0, 240);
        audio_play_piano_note_dur(659.25f, s_piano_timbre, 0, 260);
        led_effects_color_burst(255, 215, 0);
        return true;
    }

    // 2. Phím F10: Luân chuyển âm sắc (Piano trầm ấm -> Retro 8-bit -> Chuông gió Music Box)
    if (kc == 0x43) {
        s_piano_timbre = (piano_timbre_t)((s_piano_timbre + 1) % 3);
        const char *t_name = (s_piano_timbre == TIMBRE_PIANO) ? "ĐÀN PIANO TRẦM ẤM" :
                             (s_piano_timbre == TIMBRE_8BIT_MARIO) ? "RETRO GAME 8-BIT MARIO" : "CHUÔNG GIÓ MUSIC BOX";
        printf(" -> [ĐỔI ÂM SẮC: %s]\r\n", t_name);
        audio_play_piano_note_dur(523.25f, s_piano_timbre, 0, 280);
        audio_play_piano_note_dur(783.99f, s_piano_timbre, 0, 300);
        led_effects_color_burst(0, 255, 255);
        return true;
    }

    float freq = 0.0f;
    int led_idx = 0;
    const char *note_name = NULL;
    int penta_idx = -1;

    // 1. HÀNG SỐ TRÊN (14 phím: ~ .. 0, -, =, Backspace)
    if (kc == 0x35 || c == '`' || c == '~')      { freq = 493.88f; note_name = "Si (B4)"; led_idx = 0;  penta_idx = 9; }
    else if (kc == 0x1E || c == '1' || c == '!') { freq = 523.25f; note_name = "Đố (C5)"; led_idx = 2;  penta_idx = 10; }
    else if (kc == 0x1F || c == '2' || c == '@') { freq = 587.33f; note_name = "Rế (D5)"; led_idx = 4;  penta_idx = 11; }
    else if (kc == 0x20 || c == '3' || c == '#') { freq = 659.25f; note_name = "Mí (E5)"; led_idx = 6;  penta_idx = 12; }
    else if (kc == 0x21 || c == '4' || c == '$') { freq = 698.46f; note_name = "Phá (F5)"; led_idx = 8;  penta_idx = 13; }
    else if (kc == 0x22 || c == '5' || c == '%') { freq = 783.99f; note_name = "Són (G5)"; led_idx = 10; penta_idx = 14; }
    else if (kc == 0x23 || c == '6' || c == '^') { freq = 880.00f; note_name = "Lá (A5)"; led_idx = 12; penta_idx = 15; }
    else if (kc == 0x24 || c == '7' || c == '&') { freq = 987.77f; note_name = "Sí (B5)"; led_idx = 14; penta_idx = 16; }
    else if (kc == 0x25 || c == '8' || c == '*') { freq = 1046.50f; note_name = "Đố (C6)"; led_idx = 16; penta_idx = 17; }
    else if (kc == 0x26 || c == '9' || c == '(') { freq = 1174.66f; note_name = "Rế (D6)"; led_idx = 18; penta_idx = 18; }
    else if (kc == 0x27 || c == '0' || c == ')') { freq = 1318.51f; note_name = "Mí (E6)"; led_idx = 20; penta_idx = 19; }
    else if (kc == 0x2D || c == '-' || c == '_') { freq = 1396.91f; note_name = "Phá (F6)"; led_idx = 22; penta_idx = 17; }
    else if (kc == 0x2E || c == '=' || c == '+') { freq = 1567.98f; note_name = "Són (G6)"; led_idx = 24; penta_idx = 18; }
    else if (kc == 0x2A || c == '\b')            { freq = 1760.00f; note_name = "Lá (A6)"; led_idx = 26; penta_idx = 19; }

    // 2. HÀNG QWERTY (Tăng dần mượt mà từ trái sang phải: C4 -> G#5)
    else if (kc == 0x14 || upper == 'Q') { freq = 261.63f; note_name = "Đồ (C4)"; led_idx = 1;  penta_idx = 7; }
    else if (kc == 0x1A || upper == 'W') { freq = 277.18f; note_name = "Đồ# (C#4)"; led_idx = 3;  penta_idx = 8; }
    else if (kc == 0x08 || upper == 'E') { freq = 311.13f; note_name = "Rê# (D#4)"; led_idx = 5;  penta_idx = 9; }
    else if (kc == 0x15 || upper == 'R') { freq = 349.23f; note_name = "Pha (F4)"; led_idx = 7;  penta_idx = 10; }
    else if (kc == 0x17 || upper == 'T') { freq = 369.99f; note_name = "Pha# (F#4)"; led_idx = 9;  penta_idx = 11; }
    else if (kc == 0x1C || upper == 'Y') { freq = 415.30f; note_name = "Son# (G#4)"; led_idx = 11; penta_idx = 12; }
    else if (kc == 0x18 || upper == 'U') { freq = 466.16f; note_name = "La# (A#4)"; led_idx = 13; penta_idx = 13; }
    else if (kc == 0x0C || upper == 'I') { freq = 523.25f; note_name = "Đố (C5)"; led_idx = 15; penta_idx = 14; }
    else if (kc == 0x12 || upper == 'O') { freq = 554.37f; note_name = "Đố# (C#5)"; led_idx = 17; penta_idx = 15; }
    else if (kc == 0x13 || upper == 'P') { freq = 622.25f; note_name = "Rế# (D#5)"; led_idx = 19; penta_idx = 16; }
    else if (kc == 0x2F || c == '[' || c == '{') { freq = 698.46f; note_name = "Phá (F5)"; led_idx = 21; penta_idx = 17; }
    else if (kc == 0x30 || c == ']' || c == '}') { freq = 739.99f; note_name = "Phá# (F#5)"; led_idx = 23; penta_idx = 18; }
    else if (kc == 0x31 || c == '\\' || c == '|' || kc == 0x32) { freq = 830.61f; note_name = "Són# (G#5)"; led_idx = 25; penta_idx = 19; }

    // 3. HÀNG ASDF (Quãng 4 Trung chuẩn, gam Đô Trưởng / Ngũ Cung)
    else if (kc == 0x39)                 { freq = 130.81f; note_name = "Caps: Đồ Trầm (C3)"; led_idx = 0; penta_idx = 0; }
    else if (kc == 0x04 || upper == 'A') { freq = 261.63f; note_name = "Đồ (C4)"; led_idx = 2;  penta_idx = 5; }
    else if (kc == 0x16 || upper == 'S') { freq = 293.66f; note_name = "Rê (D4)"; led_idx = 4;  penta_idx = 6; }
    else if (kc == 0x07 || upper == 'D') { freq = 329.63f; note_name = "Mi (E4)"; led_idx = 6;  penta_idx = 7; }
    else if (kc == 0x09 || upper == 'F') { freq = 349.23f; note_name = "Pha (F4)"; led_idx = 8;  penta_idx = 8; }
    else if (kc == 0x0A || upper == 'G') { freq = 392.00f; note_name = "Son (G4)"; led_idx = 10; penta_idx = 9; }
    else if (kc == 0x0B || upper == 'H') { freq = 440.00f; note_name = "La (A4)"; led_idx = 12; penta_idx = 10; }
    else if (kc == 0x0D || upper == 'J') { freq = 493.88f; note_name = "Si (B4)"; led_idx = 14; penta_idx = 11; }
    else if (kc == 0x0E || upper == 'K') { freq = 523.25f; note_name = "Đố (C5)"; led_idx = 16; penta_idx = 12; }
    else if (kc == 0x0F || upper == 'L') { freq = 587.33f; note_name = "Rế (D5)"; led_idx = 18; penta_idx = 13; }
    else if (kc == 0x33 || c == ';' || c == ':') { freq = 659.25f; note_name = "Mí (E5)"; led_idx = 20; penta_idx = 14; }
    else if (kc == 0x34 || c == '\'' || c == '"') { freq = 698.46f; note_name = "Phá (F5)"; led_idx = 22; penta_idx = 15; }
    else if (kc == 0x28 || c == '\r' || c == '\n') { freq = 523.25f; note_name = "Enter: Hợp Âm Đố (C5)"; led_idx = 15; penta_idx = 10; }

    // 4. HÀNG ZXCV (Quãng 3 Trầm Ấm)
    else if (kc == 0xE1)                 { freq = 130.81f; note_name = "Shift Trái: Đồ (C3)"; led_idx = 0; penta_idx = 0; }
    else if (kc == 0x1D || upper == 'Z') { freq = 130.81f; note_name = "Đồ (C3 Trầm)"; led_idx = 1;  penta_idx = 0; }
    else if (kc == 0x1B || upper == 'X') { freq = 146.83f; note_name = "Rê (D3 Trầm)"; led_idx = 3;  penta_idx = 1; }
    else if (kc == 0x06 || upper == 'C') { freq = 164.81f; note_name = "Mi (E3 Trầm)"; led_idx = 5;  penta_idx = 2; }
    else if (kc == 0x19 || upper == 'V') { freq = 174.61f; note_name = "Pha (F3 Trầm)"; led_idx = 7;  penta_idx = 3; }
    else if (kc == 0x05 || upper == 'B') { freq = 196.00f; note_name = "Son (G3 Trầm)"; led_idx = 9;  penta_idx = 4; }
    else if (kc == 0x11 || upper == 'N') { freq = 220.00f; note_name = "La (A3 Trầm)"; led_idx = 11; penta_idx = 5; }
    else if (kc == 0x10 || upper == 'M') { freq = 246.94f; note_name = "Si (B3 Trầm)"; led_idx = 13; penta_idx = 6; }
    else if (kc == 0x36 || c == ',' || c == '<') { freq = 261.63f; note_name = "Đồ (C4)"; led_idx = 15; penta_idx = 7; }
    else if (kc == 0x37 || c == '.' || c == '>') { freq = 293.66f; note_name = "Rê (D4)"; led_idx = 17; penta_idx = 8; }
    else if (kc == 0x38 || c == '/' || c == '?') { freq = 329.63f; note_name = "Mi (E4)"; led_idx = 19; penta_idx = 9; }
    else if (kc == 0xE5)                 { freq = 349.23f; note_name = "Shift Phải: Pha (F4)"; led_idx = 21; penta_idx = 10; }

    // 5. CỤM PHÍM SỐ NUMPAD BÊN PHẢI (0..9, +, -, *, /, .)
    else if (kc >= 0x54 && kc <= 0x63) {
        switch (kc) {
            case 0x59: freq = 261.63f; note_name = "Numpad 1: Đồ (C4)"; led_idx = 0;  penta_idx = 5; break;
            case 0x5A: freq = 293.66f; note_name = "Numpad 2: Rê (D4)"; led_idx = 2;  penta_idx = 6; break;
            case 0x5B: freq = 329.63f; note_name = "Numpad 3: Mi (E4)"; led_idx = 4;  penta_idx = 7; break;
            case 0x5C: freq = 349.23f; note_name = "Numpad 4: Pha (F4)"; led_idx = 6;  penta_idx = 8; break;
            case 0x5D: freq = 392.00f; note_name = "Numpad 5: Son (G4)"; led_idx = 8;  penta_idx = 9; break;
            case 0x5E: freq = 440.00f; note_name = "Numpad 6: La (A4)"; led_idx = 10; penta_idx = 10; break;
            case 0x5F: freq = 493.88f; note_name = "Numpad 7: Si (B4)"; led_idx = 12; penta_idx = 11; break;
            case 0x60: freq = 523.25f; note_name = "Numpad 8: Đố (C5)"; led_idx = 14; penta_idx = 12; break;
            case 0x61: freq = 587.33f; note_name = "Numpad 9: Rế (D5)"; led_idx = 16; penta_idx = 13; break;
            case 0x62: freq = 220.00f; note_name = "Numpad 0: La Trầm (A3)"; led_idx = 1; penta_idx = 4; break;
            case 0x57: freq = 659.25f; note_name = "Numpad +: Mí (E5)"; led_idx = 18; penta_idx = 14; break;
            case 0x56: freq = 698.46f; note_name = "Numpad -: Phá (F5)"; led_idx = 20; penta_idx = 15; break;
            case 0x55: freq = 783.99f; note_name = "Numpad *: Són (G5)"; led_idx = 22; penta_idx = 16; break;
            case 0x54: freq = 880.00f; note_name = "Numpad /: Lá (A5)"; led_idx = 24; penta_idx = 17; break;
            case 0x63: freq = 1046.50f; note_name = "Numpad .: Đố (C6)"; led_idx = 26; penta_idx = 18; break;
            case 0x58: freq = 523.25f; note_name = "Numpad Enter: Hợp Âm Đô Trưởng!"; led_idx = 15; penta_idx = 10; break;
        }
    }

    // 6. PHÍM ĐẶC BIỆT, ĐIỀU HƯỚNG & MODIFIERS
    else if (kc == 0x2C || c == ' ') {
        freq = 261.63f; note_name = "Space: Bàn Đạp Ngân Vang (Đồ C4)"; led_idx = 14; penta_idx = 5;
    } else if (kc >= 0x4F && kc <= 0x52) { // Mũi tên
        penta_idx = 10 + (kc - 0x4F);
        freq = s_pentatonic_freqs[penta_idx];
        note_name = "Phím Mũi Tên: Lướt Âm Ngũ Cung";
        led_idx = (kc - 0x4F) * 6;
    } else if (kc == 0xE0 || kc == 0xE4) { // Ctrl
        freq = 130.81f; note_name = "Phím Ctrl: Hợp Âm Trầm Đô"; led_idx = 2; penta_idx = 0;
    } else if (kc == 0xE2 || kc == 0xE6) { // Alt
        freq = 164.81f; note_name = "Phím Alt: Hợp Âm Trầm Mi"; led_idx = 4; penta_idx = 2;
    } else if (kc == 0xE3 || kc == 0xE7) { // Win
        freq = 196.00f; note_name = "Phím Win: Hợp Âm Trầm Son"; led_idx = 6; penta_idx = 3;
    } else { // Các phím còn lại (Insert, Delete, Home, End, PageUp, PageDown, Pause, PrtSc...)
        penta_idx = 5 + (kc % 12);
        freq = s_pentatonic_freqs[penta_idx];
        note_name = "Phím Chức Năng: Nốt Chuông Hòa Âm";
        led_idx = kc % 28;
    }

    // Nếu đang bật chế độ Ngũ Cung (Pentatonic), ép toàn bộ nốt sang thang 5 âm Đô Trưởng (C-D-E-G-A)
    if (s_pentatonic_mode && penta_idx >= 0 && penta_idx < 20) {
        freq = s_pentatonic_freqs[penta_idx];
        note_name = s_pentatonic_names[penta_idx];
    }

    audio_play_piano_note_key(freq, s_piano_timbre, kc);
    uint8_t r = (led_idx * 23) % 255;
    uint8_t g = (255 - led_idx * 15) % 255;
    uint8_t b = (led_idx * 37) % 255;
    led_effects_note(led_idx, 28, r, g, b);
    printf(" -> [PIANO: %s - %.1f Hz]\r\n", note_name, freq);
    return true;
}

static void mode_piano_handle_key_release(uint8_t kc) {
    audio_piano_key_release(kc);
}

static void mode_piano_handle_action(void) {
    s_piano_timbre = (piano_timbre_t)((s_piano_timbre + 1) % 3);
    const char *t_name = (s_piano_timbre == TIMBRE_PIANO) ? "ĐÀN PIANO TRẦM ẤM" :
                         (s_piano_timbre == TIMBRE_8BIT_MARIO) ? "RETRO GAME 8-BIT MARIO" : "CHUÔNG GIÓ MUSIC BOX";
    printf("\r\n>>> [BTN 6 ACTION: ĐỔI ÂM SẮC PIANO -> %s] <<<\r\n", t_name);
    fflush(stdout);
    audio_play_piano_note_dur(523.25f, s_piano_timbre, 0, 280);
    audio_play_piano_note_dur(783.99f, s_piano_timbre, 0, 300);
    led_effects_color_burst(0, 255, 255);
}

// Cấu hình Driver cho Chế độ 1
const toy_mode_driver_t g_mode_piano_driver = {
    .id                 = 0,
    .name               = "1. ĐÀN PIANO DIỆU KỲ & NGŨ CUNG (FULL TOÀN BÀN PHÍM)",
    .short_desc         = "Toàn bộ bàn phím là các nốt Piano, hỗ trợ thang âm ngũ cung chống phô",
    .init               = NULL,
    .on_enter           = mode_piano_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_piano_handle_key,
    .handle_key_release = mode_piano_handle_key_release,
    .background_tick    = NULL,
    .handle_action      = mode_piano_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
