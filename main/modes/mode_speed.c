#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

static void mode_speed_on_enter(void) {
    printf("\r\n>>> [NHẠC CHẠY THEO TỐC ĐỘ TAY SẴN SÀNG] <<<\r\n");
    printf(">>> [Gõ phím càng nhanh, giai điệu dồn dập càng cuốn hút!] <<<\r\n\r\n");
}

static bool mode_speed_handle_key(uint8_t kc, uint8_t mod, char c) {
    audio_speed_runner_keystroke();
    led_effects_beat();
    printf(" -> [SPEED RUNNER: Nhịp nhạc lướt nhanh theo tốc độ gõ!]\r\n");
    return true;
}

static void mode_speed_background_tick(uint32_t delta_ms) {
    audio_speed_runner_idle_check();
}

// Cấu hình Driver cho Chế độ 3
const toy_mode_driver_t g_mode_speed_driver = {
    .id                 = 2,
    .name               = "3. NHẠC CHẠY THEO TỐC ĐỘ TAY",
    .short_desc         = "Tốc độ bài hát biến chuyển linh hoạt theo tốc độ gõ phím của bé",
    .init               = NULL,
    .on_enter           = mode_speed_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_speed_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_speed_background_tick,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
