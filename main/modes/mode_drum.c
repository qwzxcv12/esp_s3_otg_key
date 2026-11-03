#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

static void mode_drum_on_enter(void) {
    printf("\r\n>>> [DÀN TRỐNG BEAT-MAKER & DJ TOÀN DẢI SẴN SÀNG] <<<\r\n");
    printf(">>> [Space/B/Z/X/C/V = Bass Kick | S/A/D/F/G = Snare | H/J/K/L = Hi-Hat | ;/'/Numpad = Cymbal | Enter = Clap] <<<\r\n\r\n");
}

static bool mode_drum_handle_key(uint8_t kc, uint8_t mod, char c) {
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 1. Nhóm Trống Bass Kick (Space, b, z, x, c, v, Numpad 0)
    if (c == ' ' || upper == 'B' || upper == 'Z' || upper == 'X' || upper == 'C' || upper == 'V' || kc == 0x62) {
        audio_play_drum(DRUM_KICK);
        led_effects_beat();
        printf(" -> [DÀN TRỐNG: TRỐNG BASS KICK - BÙM BÙM!]\r\n");
    }
    // 2. Nhóm Trống Snare (s, a, d, f, g, Numpad 1, 2)
    else if (upper == 'S' || upper == 'A' || upper == 'D' || upper == 'F' || upper == 'G' || kc == 0x59 || kc == 0x5A) {
        audio_play_drum(DRUM_SNARE);
        led_effects_color_burst(255, 255, 255);
        printf(" -> [DÀN TRỐNG: TRỐNG CON SNARE - TÁCH CHÁT!]\r\n");
    }
    // 3. Nhóm Chuông Lắc Hi-Hat (h, j, k, l, Numpad 4, 5)
    else if (upper == 'H' || upper == 'J' || upper == 'K' || upper == 'L' || kc == 0x5C || kc == 0x5D) {
        audio_play_drum(DRUM_HIHAT);
        led_effects_color_burst(255, 255, 0);
        printf(" -> [DÀN TRỐNG: CHUÔNG LẮC HI-HAT - XÌ CHÍT!]\r\n");
    }
    // 4. Nhóm Chũm Chọe Cymbal (;, ', Numpad 7, 8)
    else if (c == ';' || c == '\'' || kc == 0x5F || kc == 0x60) {
        audio_play_drum(DRUM_CYMBAL);
        led_effects_color_burst(0, 255, 255);
        printf(" -> [DÀN TRỐNG: CHŨM CHỌE CYMBAL - XOANGGG!]\r\n");
    }
    // 5. Nhóm Trống Tom (t, q, w, e, r, y, u, i, o, p, Numpad 3, 6, 9)
    else if (upper == 'T' || upper == 'Q' || upper == 'W' || upper == 'E' || upper == 'R' || upper == 'Y' ||
             upper == 'U' || upper == 'I' || upper == 'O' || upper == 'P' || kc == 0x5B || kc == 0x5E || kc == 0x61) {
        audio_play_drum(DRUM_TOM);
        led_effects_color_burst(255, 128, 0);
        printf(" -> [DÀN TRỐNG: TRỐNG TOM - TÙNG TÙNG!]\r\n");
    }
    // 6. Tiếng vỗ tay Clap (Enter, Numpad Enter)
    else if (c == '\r' || c == '\n' || kc == 0x58) {
        audio_play_drum(DRUM_CLAP);
        led_effects_color_burst(255, 0, 128);
        printf(" -> [DÀN TRỐNG: TIẾNG VỖ TAY HOAN HÔ RÀO RÀO!]\r\n");
    }
    // 7. Tất cả các phím khác (Mũi tên, F-keys, Numpad +, -, *, /)
    else {
        audio_play_drum((drum_type_t)(kc % 6));
        led_effects_spark();
        printf(" -> [DÀN TRỐNG: BỘ GÕ TIẾT TẤU DJ!]\r\n");
    }
    return true;
}

// Cấu hình Driver cho Chế độ 4
const toy_mode_driver_t g_mode_drum_driver = {
    .id                 = 3,
    .name               = "4. DÀN TRỐNG BEAT-MAKER & DJ TOÀN DẢI",
    .short_desc         = "Dàn trống điện tử: Kick, Snare, Hihat, Cymbal, Tom và tiếng vỗ tay hoan hô",
    .init               = NULL,
    .on_enter           = mode_drum_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_drum_handle_key,
    .handle_key_release = NULL,
    .background_tick    = NULL,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
