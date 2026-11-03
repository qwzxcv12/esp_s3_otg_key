#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

static void mode_typewriter_on_enter(void) {
    printf("\r\n>>> [MÁY GÕ CHỮ CỔ ĐIỂN SẴN SÀNG] <<<\r\n");
    printf(">>> [Gõ phím cơ khí tách cạch | Phím Space kéo trục giấy | Phím Enter chuông kenggg!] <<<\r\n\r\n");
}

static bool mode_typewriter_handle_key(uint8_t kc, uint8_t mod, char c) {
    if (c == ' ') {
        audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
        led_effects_spark();
        printf(" -> [MÁY GÕ CHỮ: KÉO TRỤC GIẤY XOẠTTT]\r\n");
    } else if (c == '\r' || c == '\n' || kc == 0x58) {
        audio_play_typewriter(TYPEWRITER_ENTER_BELL);
        led_effects_color_burst(255, 215, 0);
        printf(" -> [MÁY GÕ CHỮ: CHUÔNG CƠ KHÍ KENGGG!]\r\n");
    } else if (c == '\b' || kc == 0x4C) { // Backspace or Delete
        audio_play_typewriter(TYPEWRITER_BACKSPACE);
        printf(" -> [MÁY GÕ CHỮ: TẨY XÓA ỤT ỊT]\r\n");
    } else if (kc == 0x2B) { // Tab
        audio_play_typewriter(TYPEWRITER_KEY_CLICK);
        printf(" -> [MÁY GÕ CHỮ: THỤT ĐẦU DÒNG CẠCH CẠCH]\r\n");
    } else {
        audio_play_typewriter(TYPEWRITER_KEY_CLICK);
        led_effects_spark();
        printf(" -> [MÁY GÕ CHỮ: BÚA KIM LOẠI TÁCH CẠCH!]\r\n");
    }
    return true;
}

// Cấu hình Driver cho Chế độ 5
const toy_mode_driver_t g_mode_typewriter_driver = {
    .id                 = 4,
    .name               = "5. MÁY GÕ CHỮ CỔ ĐIỂN",
    .short_desc         = "Tái hiện âm thanh máy gõ chữ: búa kim loại, chuông keng và trục giấy",
    .init               = NULL,
    .on_enter           = mode_typewriter_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_typewriter_handle_key,
    .handle_key_release = NULL,
    .background_tick    = NULL,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
