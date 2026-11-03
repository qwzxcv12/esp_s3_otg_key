#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

static bool s_backing_beat_on = false;
static uint32_t s_beat_acc_ms = 0;
static uint8_t s_beat_step = 0;

static void mode_drum_on_enter(void) {
    s_backing_beat_on = false;
    s_beat_acc_ms = 0;
    s_beat_step = 0;
    printf("\r\n>>> [DÀN TRỐNG BEAT-MAKER & DJ TOÀN DẢI SẴN SÀNG] <<<\r\n");
    printf(">>> [Space/Z..M = Bass Kick | A..G = Snare | H..L = Hi-Hat | Q..P = Dàn Trống Tom Trầm->Cao | 1..0/;/' = Cymbal | Enter = Vỗ Tay] <<<\r\n");
    printf(">>> [Phím TAB hoặc F10: Bật/Tắt Nhịp Nền DJ 120 BPM để bé gõ trống theo nhạc!] <<<\r\n\r\n");
}

static void mode_drum_on_exit(void) {
    s_backing_beat_on = false;
    s_beat_acc_ms = 0;
}

static bool mode_drum_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 0. Phím TAB hoặc F10: Bật / Tắt Nhịp Nền DJ (Backing Beat Loop 120 BPM)
    if (kc == 0x2B || kc == 0x43) {
        s_backing_beat_on = !s_backing_beat_on;
        s_beat_acc_ms = 0;
        s_beat_step = 0;
        printf("\r\n>>> [DÀN TRỐNG DJ: %s NHỊP TRỐNG NỀN TỰ ĐỘNG (120 BPM)] <<<\r\n",
               s_backing_beat_on ? "BẬT" : "TẮT");
        audio_play_drum_pitched(DRUM_CYMBAL, 2);
        led_effects_color_burst(s_backing_beat_on ? 0 : 255, 255, s_backing_beat_on ? 120 : 0);
        return true;
    }

    // 1. Nhóm Trống Bass Kick (Space, Z, X, C, V, B, N, M, Numpad 0) -> Cao độ trầm hùng khác nhau
    if (c == ' ' || upper == 'Z' || upper == 'X' || upper == 'C' || upper == 'V' ||
        upper == 'B' || upper == 'N' || upper == 'M' || kc == 0x62) {
        int8_t semi = 0;
        if (upper == 'Z') semi = -3;
        else if (upper == 'X') semi = -2;
        else if (upper == 'C') semi = -1;
        else if (upper == 'V' || c == ' ') semi = 0;
        else if (upper == 'B') semi = 1;
        else if (upper == 'N') semi = 2;
        else if (upper == 'M') semi = 3;
        audio_play_drum_pitched(DRUM_KICK, semi);
        led_effects_beat();
        printf(" -> [DÀN TRỐNG: TRỐNG BASS KICK (%+d) - BÙM BÙM!]\r\n", (int)semi);
    }
    // 2. Nhóm Trống Snare (A, S, D, F, G, Numpad 1, 2) -> Snare trầm tới đanh gọn
    else if (upper == 'A' || upper == 'S' || upper == 'D' || upper == 'F' || upper == 'G' ||
             kc == 0x59 || kc == 0x5A) {
        int8_t semi = 0;
        if (upper == 'A') semi = -3;
        else if (upper == 'S') semi = -1;
        else if (upper == 'D') semi = 0;
        else if (upper == 'F') semi = 2;
        else if (upper == 'G') semi = 4;
        audio_play_drum_pitched(DRUM_SNARE, semi);
        led_effects_color_burst(255, 255, 255);
        printf(" -> [DÀN TRỐNG: TRỐNG CON SNARE (%+d) - TÁCH CHÁT!]\r\n", (int)semi);
    }
    // 3. Nhóm Chuông Lắc Hi-Hat (H, J, K, L, Numpad 4, 5)
    else if (upper == 'H' || upper == 'J' || upper == 'K' || upper == 'L' || kc == 0x5C || kc == 0x5D) {
        int8_t semi = 0;
        if (upper == 'H') semi = -2;
        else if (upper == 'J') semi = 0;
        else if (upper == 'K') semi = 2;
        else if (upper == 'L') semi = 5;
        audio_play_drum_pitched(DRUM_HIHAT, semi);
        led_effects_color_burst(255, 255, 0);
        printf(" -> [DÀN TRỐNG: CHUÔNG LẮC HI-HAT (%+d) - XÌ CHÍT!]\r\n", (int)semi);
    }
    // 4. Nhóm Chũm Chọe Cymbal (Hàng phím số 1..0, ;, ', Numpad 7, 8)
    else if ((c >= '0' && c <= '9') || c == ';' || c == '\'' || kc == 0x5F || kc == 0x60) {
        int8_t semi = 0;
        if (c >= '1' && c <= '9') semi = (int8_t)((c - '5') * 1);
        else if (c == '0') semi = 5;
        else if (c == ';') semi = -2;
        else if (c == '\'') semi = 3;
        audio_play_drum_pitched(DRUM_CYMBAL, semi);
        led_effects_color_burst(0, 255, 255);
        printf(" -> [DÀN TRỐNG: CHŨM CHỌE CYMBAL (%+d) - XOANGGG!]\r\n", (int)semi);
    }
    // 5. Dàn 10 Trống Tom (Q, W, E, R, T, Y, U, I, O, P, Numpad 3, 6, 9) -> Trải dài từ Floor Tom trầm tới High Tom cao!
    else if (upper == 'Q' || upper == 'W' || upper == 'E' || upper == 'R' || upper == 'T' ||
             upper == 'Y' || upper == 'U' || upper == 'I' || upper == 'O' || upper == 'P' ||
             kc == 0x5B || kc == 0x5E || kc == 0x61) {
        int8_t semi = 0;
        if (upper == 'Q') semi = -6;
        else if (upper == 'W') semi = -4;
        else if (upper == 'E') semi = -3;
        else if (upper == 'R') semi = -1;
        else if (upper == 'T') semi = 0;
        else if (upper == 'Y') semi = 2;
        else if (upper == 'U') semi = 3;
        else if (upper == 'I') semi = 5;
        else if (upper == 'O') semi = 7;
        else if (upper == 'P') semi = 9;
        audio_play_drum_pitched(DRUM_TOM, semi);
        led_effects_color_burst(255, 128, 0);
        printf(" -> [DÀN TRỐNG: TRỐNG TOM (%+d) - TÙNG TÙNG!]\r\n", (int)semi);
    }
    // 6. Tiếng vỗ tay Clap (Enter, Backspace, Numpad Enter)
    else if (c == '\r' || c == '\n' || c == '\b' || kc == 0x58 || kc == 0x2A) {
        int8_t semi = (kc == 0x2A || c == '\b') ? 3 : 0;
        audio_play_drum_pitched(DRUM_CLAP, semi);
        led_effects_color_burst(255, 0, 128);
        printf(" -> [DÀN TRỐNG: TIẾNG VỖ TAY HOAN HÔ RÀO RÀO!]\r\n");
    }
    // 7. Tất cả các phím khác (Mũi tên, F-keys, Numpad +, -, *, /)
    else {
        drum_type_t dt = (drum_type_t)(kc % 6);
        int8_t semi = (int8_t)((kc % 7) - 3);
        audio_play_drum_pitched(dt, semi);
        led_effects_spark();
        printf(" -> [DÀN TRỐNG: BỘ GÕ TIẾT TẤU DJ!]\r\n");
    }
    return true;
}

static void mode_drum_background_tick(uint32_t delta_ms) {
    if (!s_backing_beat_on) return;

    s_beat_acc_ms += delta_ms;
    // 120 BPM -> mỗi nốt móc đơn (1/8 note) là 250ms
    if (s_beat_acc_ms >= 250) {
        s_beat_acc_ms -= 250;
        switch (s_beat_step & 0x07) {
            case 0: // Phách 1: Kick
                audio_play_drum_pitched(DRUM_KICK, -1);
                led_effects_beat();
                break;
            case 1: // Hi-hat nhẹ
                audio_play_drum_pitched(DRUM_HIHAT, 2);
                break;
            case 2: // Phách 2: Snare
                audio_play_drum_pitched(DRUM_SNARE, 0);
                break;
            case 3: // Hi-hat
                audio_play_drum_pitched(DRUM_HIHAT, 2);
                break;
            case 4: // Phách 3: Kick
                audio_play_drum_pitched(DRUM_KICK, 0);
                led_effects_beat();
                break;
            case 5: // Kick phụ + Hi-hat
                audio_play_drum_pitched(DRUM_KICK, 2);
                break;
            case 6: // Phách 4: Snare + Clap
                audio_play_drum_pitched(DRUM_SNARE, 1);
                break;
            case 7: // Hi-hat mở nhẹ
                audio_play_drum_pitched(DRUM_HIHAT, -2);
                break;
        }
        s_beat_step = (s_beat_step + 1) & 0x07;
    }
}

// Cấu hình Driver cho Chế độ 4
const toy_mode_driver_t g_mode_drum_driver = {
    .id                 = 3,
    .name               = "4. DÀN TRỐNG BEAT-MAKER & DJ TOÀN DẢI",
    .short_desc         = "Dàn trống điện tử đa cao độ + Nhịp nền DJ tự động (Phím Tab)",
    .init               = NULL,
    .on_enter           = mode_drum_on_enter,
    .on_exit            = mode_drum_on_exit,
    .handle_key         = mode_drum_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_drum_background_tick,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
