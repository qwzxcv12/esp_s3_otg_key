#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

#define TYPEWRITER_MAX_COL          32  // 4 cạnh x 8 chữ = 32 LED trọn 1 khung trang giấy vuông
#define TYPEWRITER_MARGIN_BELL_COL  26  // Chuông báo sắp hết trang tại chữ thứ 26 (trên cạnh thứ 4)

static uint8_t s_carriage_col = 0;

static void mode_typewriter_on_enter(void) {
    s_carriage_col = 0;
    printf("\r\n>>> [MÁY GÕ CHỮ CỔ ĐIỂN TRÊN KHUNG TRANG GIẤY VUÔNG 4x8 (32 LED)] <<<\r\n");
    printf(">>> [Mỗi cạnh 8 LED là 1 dòng chữ (4 dòng x 8 chữ = 32 cột) | Cột 26 reo chuông báo hết lề | Cột 32 hoặc Enter trượt trục giấy về đầu!] <<<\r\n\r\n");
    led_effects_typewriter_key(s_carriage_col, TYPEWRITER_MAX_COL, false);
}

static bool mode_typewriter_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;

    // 1. Phím Enter hoặc Home: Trượt trục giấy mượt mà ngược quanh khung vuông về đầu trang (Cột 0)
    if (c == '\r' || c == '\n' || kc == 0x58 || kc == 0x4A) {
        uint8_t prev = s_carriage_col;
        s_carriage_col = 0;
        audio_play_typewriter(TYPEWRITER_ENTER_BELL);
        audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
        led_effects_typewriter_return(prev);
        printf(" -> [MÁY GÕ CHỮ: CHUÔNG KENGGG & KÉO TRỤC GIẤY VỀ ĐẦU TRANG (Cột 0/32)!]\r\n");
        return true;
    }

    // 2. Phím Backspace, Delete hoặc Mũi tên Trái: Lùi con trỏ giấy 1 cột kèm tia lửa tẩy xóa
    if (c == '\b' || kc == 0x2A || kc == 0x4C || kc == 0x50) {
        if (s_carriage_col > 0) {
            s_carriage_col--;
        }
        audio_play_typewriter(TYPEWRITER_BACKSPACE);
        led_effects_typewriter_backspace(s_carriage_col, TYPEWRITER_MAX_COL);
        printf(" -> [MÁY GÕ CHỮ: LÙI GIẤY TẨY XÓA (Cột %u/%u - Dòng %u)]\r\n",
               (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL,
               (unsigned int)(s_carriage_col / 8 + 1));
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
            led_effects_typewriter_return(prev_col);
            printf(" -> [MÁY GÕ CHỮ: TAB CHẠM LỀ CUỐI -> TỰ ĐỘNG KÉO GIẤY SANG TRANG MỚI!]\r\n");
        } else {
            audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
            if (prev_col < TYPEWRITER_MARGIN_BELL_COL && s_carriage_col >= TYPEWRITER_MARGIN_BELL_COL) {
                audio_play_typewriter(TYPEWRITER_ENTER_BELL);
                printf(" -> [MÁY GÕ CHỮ: TAB + CHUÔNG BÁO SẮP ĐẦY TRANG! (Cột %u/%u)]\r\n",
                       (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
            } else {
                printf(" -> [MÁY GÕ CHỮ: THỤT ĐẦU DÒNG TAB (Cột %u/%u)]\r\n",
                       (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
            }
            led_effects_typewriter_key(s_carriage_col, TYPEWRITER_MAX_COL, true);
        }
        return true;
    }

    // 4. Phím Space hoặc bất kỳ phím gõ chữ nào khác: Tiến 1 cột giấy trên khung vuông 32 LED
    uint8_t prev_col = s_carriage_col;
    s_carriage_col++;

    if (s_carriage_col >= TYPEWRITER_MAX_COL) {
        // Đã gõ đầy 32 LED (4 cạnh x 8 chữ) -> Tự động kéo trục giấy ngược vòng về 0!
        s_carriage_col = 0;
        audio_play_typewriter(TYPEWRITER_ENTER_BELL);
        audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
        led_effects_typewriter_return(prev_col);
        printf(" -> [MÁY GÕ CHỮ: ĐẦY 4 DÒNG (32 CỘT) -> KENGGG! TỰ ĐỘNG KÉO TRỤC GIẤY VỀ 0!]\r\n");
        return true;
    }

    if (c == ' ') {
        audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
    } else {
        audio_play_typewriter(TYPEWRITER_KEY_CLICK);
    }

    // Khi vừa gõ hết 1 cạnh 8 chữ (cột 8, 16, 24) hoặc chạm cột 26 báo hết lề
    if (s_carriage_col == TYPEWRITER_MARGIN_BELL_COL) {
        audio_play_typewriter(TYPEWRITER_ENTER_BELL);
        printf(" -> [MÁY GÕ CHỮ: KENGGG! CHUÔNG BÁO SẮP HẾT CẠNH CUỐI (Cột %u/%u)]\r\n",
               (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
    } else if ((s_carriage_col & 0x07) == 0) {
        printf(" -> [MÁY GÕ CHỮ: QUA GÓC VUÔNG -> SANG DÒNG CẠNH %u (Cột %u/%u)]\r\n",
               (unsigned int)(s_carriage_col / 8 + 1),
               (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
    } else if (c == ' ') {
        printf(" -> [MÁY GÕ CHỮ: KÉO TRỤC GIẤY CÁCH CHỮ (Cột %u/%u)]\r\n",
               (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL);
    } else {
        printf(" -> [MÁY GÕ CHỮ: BÚA KIM LOẠI TÁCH CẠCH! (Cột %u/%u - Cạnh %u)]\r\n",
               (unsigned int)s_carriage_col, (unsigned int)TYPEWRITER_MAX_COL,
               (unsigned int)(s_carriage_col / 8 + 1));
    }

    led_effects_typewriter_key(s_carriage_col, TYPEWRITER_MAX_COL, (c == ' '));
    return true;
}

static void mode_typewriter_handle_action(void) {
    uint8_t prev = s_carriage_col;
    s_carriage_col = 0;
    audio_play_typewriter_return();
    printf("\r\n>>> [BTN 6 ACTION: MÁY GÕ CHỮ KÉO TRỤC GIẤY VỀ ĐẦU DÒNG (KENG KENG!)] <<<\r\n");
    fflush(stdout);
    led_effects_typewriter_return(prev);
}

const toy_mode_driver_t g_mode_typewriter_driver = {
    .id                 = 4,
    .name               = "5. MÁY GÕ CHỮ CỔ ĐIỂN",
    .short_desc         = "Trang giấy vuông 4 dòng x 8 chữ (32 LED), tia lửa búa in & kéo trục giấy xoay vòng",
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
