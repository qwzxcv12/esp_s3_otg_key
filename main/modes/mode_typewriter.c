#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

#define TYPEWRITER_MAX_COL 30
#define TYPEWRITER_MARGIN_BELL_COL 25

static uint8_t s_carriage_col = 0;

static void mode_typewriter_on_enter(void) {
    s_carriage_col = 0;
    printf("\r\n>>> [MÁY GÕ CHỮ CỔ ĐIỂN SẴN SÀNG] <<<\r\n");
    printf(">>> [Gõ phím cơ khí tiến từng cột giấy trên 30 bóng LED | Cột 25 tự reo chuông báo hết lề | Cột 30 hoặc Enter tự kéo trục giấy về đầu dòng!] <<<\r\n\r\n");
    led_effects_carriage(s_carriage_col, TYPEWRITER_MAX_COL);
}

static bool mode_typewriter_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;

    // 1. Phím Enter hoặc Home: Kéo trục giấy về đầu dòng (Carriage Return + Chuông Keng!)
    if (c == '\r' || c == '\n' || kc == 0x58 || kc == 0x4A) {
        s_carriage_col = 0;
        audio_play_typewriter(TYPEWRITER_ENTER_BELL);
        audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
        led_effects_color_burst(255, 215, 0);
        led_effects_carriage(s_carriage_col, TYPEWRITER_MAX_COL);
        printf(" -> [MÁY GÕ CHỮ: CHUÔNG KENGGG & KÉO TRỤC GIẤY VỀ ĐẦU DÒNG (Cột 0/30)!]\r\n");
        return true;
    }

    // 2. Phím Backspace, Delete hoặc Mũi tên Trái: Lùi con trỏ giấy 1 cột
    if (c == '\b' || kc == 0x2A || kc == 0x4C || kc == 0x50) {
        if (s_carriage_col > 0) {
            s_carriage_col--;
        }
        audio_play_typewriter(TYPEWRITER_BACKSPACE);
        led_effects_carriage(s_carriage_col, TYPEWRITER_MAX_COL);
        printf(" -> [MÁY GÕ CHỮ: LÙI GIẤY TẨY XÓA (Cột %u/%u)]\r\n",
               (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
        return true;
    }

    // 3. Phím Tab: Nhảy 4 cột giấy (Tab Stop)
    if (kc == 0x2B) {
        uint8_t prev_col = s_carriage_col;
        s_carriage_col += 4;
        if (s_carriage_col >= TYPEWRITER_MAX_COL) {
            s_carriage_col = 0;
            audio_play_typewriter(TYPEWRITER_ENTER_BELL);
            audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
            led_effects_color_burst(255, 215, 0);
            led_effects_carriage(s_carriage_col, TYPEWRITER_MAX_COL);
            printf(" -> [MÁY GÕ CHỮ: TAB CHẠM LỀ CUỐI -> TỰ ĐỘNG XUỐNG DÒNG MỚI!]\r\n");
        } else {
            audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
            if (prev_col < TYPEWRITER_MARGIN_BELL_COL && s_carriage_col >= TYPEWRITER_MARGIN_BELL_COL) {
                audio_play_typewriter(TYPEWRITER_ENTER_BELL);
                printf(" -> [MÁY GÕ CHỮ: TAB + CHUÔNG BÁO SẮP HẾT LỀ GIẤY! (Cột %u/%u)]\r\n",
                       (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
            } else {
                printf(" -> [MÁY GÕ CHỮ: THỤT ĐẦU DÒNG TAB (Cột %u/%u)]\r\n",
                       (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
            }
            led_effects_carriage(s_carriage_col, TYPEWRITER_MAX_COL);
        }
        return true;
    }

    // 4. Phím Space hoặc bất kỳ phím gõ chữ nào khác: Tiến 1 cột giấy
    s_carriage_col++;

    if (s_carriage_col >= TYPEWRITER_MAX_COL) {
        // Đã gõ hết dòng 30 ký tự -> Tự động kéo trục giấy về cột 0 và reo chuông!
        s_carriage_col = 0;
        audio_play_typewriter(TYPEWRITER_ENTER_BELL);
        audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
        led_effects_color_burst(255, 215, 0);
        led_effects_carriage(s_carriage_col, TYPEWRITER_MAX_COL);
        printf(" -> [MÁY GÕ CHỮ: HẾT DÒNG 30 CỘT -> KENGGG! TỰ ĐỘNG XUỐNG DÒNG MỚI!]\r\n");
        return true;
    }

    if (c == ' ') {
        audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
    } else {
        audio_play_typewriter(TYPEWRITER_KEY_CLICK);
    }

    // Khi chạm tới cột 25 (Margin Bell): Tự động ngân tiếng chuông báo sắp hết dòng!
    if (s_carriage_col == TYPEWRITER_MARGIN_BELL_COL) {
        audio_play_typewriter(TYPEWRITER_ENTER_BELL);
        printf(" -> [MÁY GÕ CHỮ: KENGGG! CHUÔNG BÁO SẮP HẾT LỀ GIẤY (Cột %u/%u)]\r\n",
               (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
    } else if (c == ' ') {
        printf(" -> [MÁY GÕ CHỮ: KÉO TRỤC GIẤY CÁCH CHỮ (Cột %u/%u)]\r\n",
               (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
    } else {
        printf(" -> [MÁY GÕ CHỮ: BÚA KIM LOẠI TÁCH CẠCH! (Cột %u/%u)]\r\n",
               (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
    }

    led_effects_carriage(s_carriage_col, TYPEWRITER_MAX_COL);
    return true;
}

static void mode_typewriter_handle_action(void) {
    audio_play_typewriter(TYPEWRITER_ENTER_BELL);
    vTaskDelay(pdMS_TO_TICKS(100));
    audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
    printf("\r\n>>> [BTN 6 ACTION: MÁY GÕ CHỮ XUỐNG DÒNG (KENG KENG!)] <<<\r\n");
    fflush(stdout);
    led_effects_spark();
}

// Cấu hình Driver cho Chế độ 5
const toy_mode_driver_t g_mode_typewriter_driver = {
    .id                 = 4,
    .name               = "5. MÁY GÕ CHỮ CỔ ĐIỂN",
    .short_desc         = "Tái hiện máy gõ chữ cơ khí: trục giấy chạy trên 30 LED, chuông báo hết lề và tự xuống dòng",
    .init               = NULL,
    .on_enter           = mode_typewriter_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_typewriter_handle_key,
    .handle_key_release = NULL,
    .background_tick    = NULL,
    .handle_action      = mode_typewriter_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
