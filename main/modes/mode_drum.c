#include <stdio.h>
#include <stdbool.h>
#include "esp_timer.h"
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

static bool s_backing_beat_on = false;
static uint32_t s_beat_acc_ms = 0;
static uint8_t s_beat_step = 0;
static uint16_t s_dj_bpm = 120;           // Tốc độ nhịp điệu BPM linh hoạt
static int64_t s_last_tap_time_us = 0;   // Tap Tempo bắt nhịp tự động

static void mode_drum_on_enter(void) {
    s_backing_beat_on = false;
    s_beat_acc_ms = 0;
    s_beat_step = 0;
    led_effects_drum_dj_beat(0, false);
    printf("\r\n>>> [DÀN TRỐNG BEAT-MAKER & DJ KHUNG HÌNH VUÔNG 4x8 SẴN SÀNG] <<<\r\n");
    printf(">>> [Kick = Nổ 4 Tâm Cạnh | Snare = Chớp 2 Cạnh Đối Diện | Hi-Hat = Lấp Lánh 4 Góc | Q..P = 10 Tom Xoay Quanh Hình Vuông] <<<\r\n");
    printf(">>> [Phím TAB hoặc F10: Bật/Tắt Nhịp Nền DJ 120 BPM (Đèn chạy 4 cạnh theo nhịp!)] <<<\r\n\r\n");
}

static void mode_drum_on_exit(void) {
    s_backing_beat_on = false;
    s_beat_acc_ms = 0;
    led_effects_drum_dj_beat(0, false);
}

static bool mode_drum_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 0. Phím TAB hoặc F10: Bật / Tắt Nhịp Nền DJ (Backing Beat Loop xoay 4 cạnh)
    if (kc == 0x2B || kc == 0x43) {
        s_backing_beat_on = !s_backing_beat_on;
        s_beat_acc_ms = 0;
        s_beat_step = 0;
        printf("\r\n>>> [DÀN TRỐNG DJ: %s NHỊP TRỐNG NỀN TỰ ĐỘNG (%u BPM - XOAY 4 CẠNH)] <<<\r\n",
               s_backing_beat_on ? "BẬT" : "TẮT", (unsigned int)s_dj_bpm);
        audio_play_drum_pitched(DRUM_CYMBAL, 2);
        led_effects_drum_dj_beat(0, s_backing_beat_on);
        led_effects_square_spin(s_backing_beat_on, 0, 255, 180);
        return true;
    }

    // 0.1 Phím Mũi tên Lên / Xuống: Tăng / Giảm tốc độ nhịp DJ (BPM)
    if (kc == 0x52) { // Mũi tên Lên: Tăng tốc
        s_dj_bpm = (s_dj_bpm < 180) ? (s_dj_bpm + 5) : 180;
        printf("\r\n>>> [DÀN TRỐNG DJ: TĂNG TỐC ĐỘ NHỊP -> %u BPM] <<<\r\n", (unsigned int)s_dj_bpm);
        audio_play_drum_pitched(DRUM_HIHAT, 4);
        led_effects_square_spin(true, 0, 255, 200);
        return true;
    }
    if (kc == 0x51) { // Mũi tên Xuống: Giảm tốc
        s_dj_bpm = (s_dj_bpm > 75) ? (s_dj_bpm - 5) : 75;
        printf("\r\n>>> [DÀN TRỐNG DJ: GIẢM TỐC ĐỘ NHỊP -> %u BPM] <<<\r\n", (unsigned int)s_dj_bpm);
        audio_play_drum_pitched(DRUM_HIHAT, -4);
        led_effects_square_spin(false, 255, 180, 0);
        return true;
    }

    // 0.2 Phím Enter / Numpad Enter: Drum Roll dồn trống liên thanh bùng nổ!
    if (c == '\r' || c == '\n' || kc == 0x28 || kc == 0x58) {
        for (int r = 0; r < 4; r++) {
            audio_play_drum_pitched(DRUM_SNARE, r - 2);
        }
        audio_play_drum_pitched(DRUM_CYMBAL, 3);
        led_effects_drum_cymbal(3);
        printf(" -> [DÀN TRỐNG: DRUM ROLL LIÊN THANH DỒN DẬP & NỔ TUNG CYMBAL!]\r\n");
        return true;
    }

    // 1. Nhóm Trống Bass Kick (Space, Z, X, C, V, B, N, M, Numpad 0) -> Nổ 4 tâm cạnh dạt ra 4 góc!
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
        led_effects_drum_kick(semi);
        printf(" -> [DÀN TRỐNG: TRỐNG BASS KICK (%+d) - NỔ 4 CẠNH BÙM BÙM!]\r\n", (int)semi);
    }
    // 2. Nhóm Trống Snare (A, S, D, F, G, Numpad 1, 2) -> Chớp điện quang 2 cạnh đối diện!
    else if (upper == 'A' || upper == 'S' || upper == 'D' || upper == 'F' || upper == 'G' ||
             kc == 0x59 || kc == 0x5A) {
        int8_t semi = 0;
        if (upper == 'A') semi = -3;
        else if (upper == 'S') semi = -1;
        else if (upper == 'D') semi = 0;
        else if (upper == 'F') semi = 2;
        else if (upper == 'G') semi = 4;
        audio_play_drum_pitched(DRUM_SNARE, semi);
        led_effects_drum_snare(semi);
        printf(" -> [DÀN TRỐNG: TRỐNG CON SNARE (%+d) - CHỚP ĐỐI XỨNG TÁCH CHÁT!]\r\n", (int)semi);
    }
    // 3. Nhóm Chuông Lắc Hi-Hat (H, J, K, L, Numpad 4, 5) -> Lấp lánh tia vàng ở 4 góc vuông!
    else if (upper == 'H' || upper == 'J' || upper == 'K' || upper == 'L' || kc == 0x5C || kc == 0x5D) {
        int8_t semi = 0;
        if (upper == 'H') semi = -2;
        else if (upper == 'J') semi = 0;
        else if (upper == 'K') semi = 2;
        else if (upper == 'L') semi = 5;
        audio_play_drum_pitched(DRUM_HIHAT, semi);
        led_effects_drum_hihat(semi);
        printf(" -> [DÀN TRỐNG: CHUÔNG LẮC HI-HAT (%+d) - TIA VÀNG 4 GÓC XÌ CHÍT!]\r\n", (int)semi);
    }
    // 4. Nhóm Chũm Chọe Cymbal (Hàng phím số 1..0, ;, ', Numpad 7, 8) -> Ngân rung kim loại khắp 4 cạnh!
    else if ((c >= '0' && c <= '9') || c == ';' || c == '\'' || kc == 0x5F || kc == 0x60) {
        int8_t semi = 0;
        if (c >= '1' && c <= '9') semi = (int8_t)((c - '5') * 1);
        else if (c == '0') semi = 5;
        else if (c == ';') semi = -2;
        else if (c == '\'') semi = 3;
        audio_play_drum_pitched(DRUM_CYMBAL, semi);
        led_effects_drum_cymbal(semi);
        printf(" -> [DÀN TRỐNG: CHŨM CHỌE CYMBAL (%+d) - NGÂN RUNG 4 CẠNH XOANGGG!]\r\n", (int)semi);
    }
    // 5. Dàn 10 Trống Tom (Q..P, Numpad 3, 6, 9) -> Chạy vòng quanh 4 cạnh & phản xạ qua cạnh đối diện!
    else if (upper == 'Q' || upper == 'W' || upper == 'E' || upper == 'R' || upper == 'T' ||
             upper == 'Y' || upper == 'U' || upper == 'I' || upper == 'O' || upper == 'P' ||
             kc == 0x5B || kc == 0x5E || kc == 0x61) {
        int8_t semi = 0;
        uint8_t tom_idx = 0;
        if (upper == 'Q')      { semi = -6; tom_idx = 0; }
        else if (upper == 'W') { semi = -4; tom_idx = 1; }
        else if (upper == 'E' || kc == 0x5B) { semi = -3; tom_idx = 2; }
        else if (upper == 'R') { semi = -1; tom_idx = 3; }
        else if (upper == 'T') { semi = 0;  tom_idx = 4; }
        else if (upper == 'Y' || kc == 0x5E) { semi = 2;  tom_idx = 5; }
        else if (upper == 'U') { semi = 3;  tom_idx = 6; }
        else if (upper == 'I') { semi = 5;  tom_idx = 7; }
        else if (upper == 'O' || kc == 0x61) { semi = 7;  tom_idx = 8; }
        else if (upper == 'P') { semi = 9;  tom_idx = 9; }
        audio_play_drum_pitched(DRUM_TOM, semi);
        led_effects_drum_tom(tom_idx);
        printf(" -> [DÀN TRỐNG: TRỐNG TOM #%u (%+d) - NẢY CẠNH ĐỐI DIỆN TÙNG TÙNG!]\r\n",
               (unsigned int)(tom_idx + 1), (int)semi);
    }
    // 6. Tiếng vỗ tay Clap (Enter, Backspace, Numpad Enter) -> Pháo giấy 4 cạnh
    else if (c == '\r' || c == '\n' || c == '\b' || kc == 0x58 || kc == 0x2A) {
        int8_t semi = (kc == 0x2A || c == '\b') ? 3 : 0;
        audio_play_drum_pitched(DRUM_CLAP, semi);
        led_effects_drum_clap();
        printf(" -> [DÀN TRỐNG: TIẾNG VỖ TAY PHÁO GIẤY 4 CẠNH RÀO RÀO!]\r\n");
    }
    // 7. 4 Phím Mũi Tên: Đánh trống dội trực tiếp vào từng Cạnh Hình Vuông (Trên / Phải / Dưới / Trái)
    else if (kc >= 0x4F && kc <= 0x52) {
        uint8_t side = (kc == 0x52) ? 0 : (kc == 0x4F) ? 1 : (kc == 0x51) ? 2 : 3;
        audio_play_drum_pitched(DRUM_TOM, (int8_t)(side * 3 - 3));
        led_effects_square_side_pulse(side, 255, 80, 220);
        printf(" -> [DÀN TRỐNG: DỘI SÓNG CẠNH %u HÌNH VUÔNG!]\r\n", (unsigned int)side);
    }
    // 8. Tất cả các phím khác
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
    // Mỗi nốt móc đơn (1/8 note) tính theo s_dj_bpm: (60000 / s_dj_bpm) / 2 ms
    uint32_t step_ms = (60000 / (s_dj_bpm > 0 ? s_dj_bpm : 120)) / 2;
    if (s_beat_acc_ms >= step_ms) {
        s_beat_acc_ms -= step_ms;
        led_effects_drum_dj_beat(s_beat_step, true);
        switch (s_beat_step & 0x07) {
            case 0: // Phách 1 (Cạnh Trên): Kick
                audio_play_drum_pitched(DRUM_KICK, -1);
                led_effects_drum_kick(-1);
                break;
            case 1: // Hi-hat nhẹ
                audio_play_drum_pitched(DRUM_HIHAT, 2);
                led_effects_drum_hihat(2);
                break;
            case 2: // Phách 2 (Cạnh Phải): Snare
                audio_play_drum_pitched(DRUM_SNARE, 0);
                led_effects_drum_snare(0);
                break;
            case 3: // Hi-hat
                audio_play_drum_pitched(DRUM_HIHAT, 2);
                break;
            case 4: // Phách 3 (Cạnh Dưới): Kick
                audio_play_drum_pitched(DRUM_KICK, 0);
                led_effects_drum_kick(0);
                break;
            case 5: // Kick phụ + Hi-hat
                audio_play_drum_pitched(DRUM_KICK, 2);
                led_effects_drum_hihat(0);
                break;
            case 6: // Phách 4 (Cạnh Trái): Snare + Clap
                audio_play_drum_pitched(DRUM_SNARE, 1);
                led_effects_drum_snare(1);
                break;
            case 7: // Hi-hat mở nhẹ
                audio_play_drum_pitched(DRUM_HIHAT, -2);
                break;
        }
        s_beat_step = (s_beat_step + 1) & 0x07;
    }
}

static void mode_drum_handle_action(void) {
    audio_play_drum(DRUM_CYMBAL);
    audio_play_slap_chord();
    printf("\r\n>>> [BTN 6 ACTION: DÀN TRỐNG CYMBAL CRASH & BEAT ROLL] <<<\r\n");
    fflush(stdout);
    led_effects_drum_cymbal(2);
}

const toy_mode_driver_t g_mode_drum_driver = {
    .id                 = 3,
    .name               = "4. DÀN TRỐNG BEAT-MAKER & DJ TOÀN DẢI",
    .short_desc         = "Trống Kick nổ 4 cạnh, Snare đối xứng, Hi-Hat 4 góc, 10 Tom xoay quanh khung vuông",
    .init               = NULL,
    .on_enter           = mode_drum_on_enter,
    .on_exit            = mode_drum_on_exit,
    .handle_key         = mode_drum_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_drum_background_tick,
    .handle_action      = mode_drum_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
