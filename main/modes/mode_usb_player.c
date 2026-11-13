/**
 * @file mode_usb_player.c
 * @brief Mode 10: Máy Phát Nhạc MP3 / WAV từ USB Flash Drive (USB OTG MSC)
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"
#include "mp3_player.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MODE_USB";

static int64_t s_last_action_click_time = 0;
static uint16_t s_last_reported_index = 0xFFFF;
static mp3_player_state_t s_last_reported_state = MP3_STATE_STOPPED;

static esp_err_t mode_usb_player_init(void) {
    return mp3_player_init();
}

static void mode_usb_player_on_enter(void) {
    printf("\r\n========================================================\r\n");
    printf(">>> [MODE 10: MÁY PHÁT NHẠC USB MP3 / WAV SẴN SÀNG] <<<\r\n");
    printf("========================================================\r\n");

    uint16_t total = mp3_player_get_total_tracks();
    if (total == 0) {
        printf(">>> [CẢNH BÁO]: Chưa tìm thấy bài hát. Hãy cắm USB Flash Drive (FAT32)!\r\n");
        printf(">>> Đèn báo hiệu sẽ nhấp nháy chờ cắm USB...\r\n\r\n");
        led_effects_color_burst(255, 120, 0);
    } else {
        printf(">>> Đã nhận diện %d bài hát trên USB!\r\n", total);
        printf(">>> Đang phát: [%d/%d] %s\r\n", 
               mp3_player_get_current_index() + 1, total, mp3_player_get_current_filename());
        printf("--------------------------------------------------------\r\n");
        printf("  [NÚT CỨNG / ĐIỀU KHIỂN]:\r\n");
        printf("   - BTN 6 (Action)        : Đổi sang bài kế tiếp (Next Track)\r\n");
        printf("   - BTN 5 (Mute / Pause)  : Tạm dừng / Phát tiếp (Play / Pause)\r\n");
        printf("   - BTN 1 / BTN 2         : Giảm / Tăng âm lượng\r\n");
        printf("   - BTN 4                 : Đổi kiểu đèn nháy theo nhạc (Visualizer)\r\n");
        printf("   - BTN 3                 : Chuyển sang chế độ tiếp theo\r\n");
        printf("  [NẾU CẮM BÀN PHÍM QUA HUB]:\r\n");
        printf("   - Phím SPACE            : Play / Pause\r\n");
        printf("   - Mũi tên Phải / Enter  : Next Track (vòng quay LED 360°)\r\n");
        printf("   - Mũi tên Trái          : Prev Track\r\n");
        printf("   - Phím 1..9             : Nhảy nhanh tới bài 1 đến 9\r\n");
        printf("   - Phím R                : Đổi chế độ lặp (All / 1 bài / Ngẫu nhiên)\r\n");
        printf("   - Phím S                : Bật / Tắt phát ngẫu nhiên (Shuffle)\r\n");
        printf("   - Phím V                : Đổi kiểu Visualizer LED nháy theo nhạc\r\n");
        printf("   - Phím M                : Tắt / Bật tiếng nhanh (Mute / Unmute)\r\n");
        printf("   - Phím + / -            : Tăng / Giảm âm lượng\r\n");
        printf("   - Phím E                : Bật / Tắt bộ lọc Hi-Fi DSP (Subsonic HPF + Bass + Treble)\r\n");
        printf("========================================================\r\n\r\n");

        if (mp3_player_get_state() != MP3_STATE_PLAYING) {
            mp3_player_play_index(0);
        }
        led_effects_color_burst(0, 255, 128);
    }
}

static void mode_usb_player_on_exit(void) {
    printf(">>> [MODE 10]: Tạm dừng nhạc USB trước khi rời chế độ\r\n");
    mp3_player_stop();
}

static void mode_usb_player_handle_action(void) {
    int64_t now = esp_timer_get_time();
    int64_t diff_ms = (now - s_last_action_click_time) / 1000;
    s_last_action_click_time = now;

    if (diff_ms < 450) {
        printf("\r\n>>> [BTN 6 (DOUBLE-CLICK): NEXT TRACK] <<<\r\n");
        mp3_player_next();
        led_effects_square_spin(true, 0, 220, 255);
    } else {
        printf("\r\n>>> [BTN 6 (CLICK): PLAY / PAUSE] <<<\r\n");
        mp3_player_toggle_pause();
        led_effects_color_burst(0, 255, 200);
    }
}

static bool mode_usb_player_handle_key(uint8_t kc, uint8_t mod, char c) {
    // 1. Phím Space: Play / Pause
    if (c == ' ' || kc == 0x2C) {
        mp3_player_toggle_pause();
        printf(">>> [BÀN PHÍM SPACE: PLAY / PAUSE] <<<\r\n");
        led_effects_color_burst(0, 255, 200);
        return true;
    }

    // 2. Mũi tên Phải hoặc Enter: Next
    if (kc == 0x4F || kc == 0x28 || c == '\n' || c == '\r') {
        printf(">>> [BÀN PHÍM: NEXT TRACK] <<<\r\n");
        mp3_player_next();
        led_effects_square_spin(true, 0, 220, 255);
        return true;
    }

    // 3. Mũi tên Trái: Previous Track
    if (kc == 0x50) {
        printf(">>> [BÀN PHÍM: PREVIOUS TRACK] <<<\r\n");
        mp3_player_prev();
        led_effects_square_spin(false, 200, 100, 255);
        return true;
    }

    // 4. Phím số 1..9: Nhảy nhanh tới bài hát
    if (c >= '1' && c <= '9') {
        uint16_t target = (uint16_t)(c - '1');
        if (target < mp3_player_get_total_tracks()) {
            printf(">>> [BÀN PHÍM: CHỌN BÀI SỐ %d] <<<\r\n", target + 1);
            mp3_player_play_index(target);
            led_effects_color_burst(255, 255, 0);
        } else {
            printf(">>> [BÀN PHÍM]: Chỉ có %d bài hát trong USB!\r\n", mp3_player_get_total_tracks());
            led_effects_color_burst(255, 50, 50);
        }
        return true;
    }

    // 5. Phím 'r' hoặc 'R': Đổi chế độ lặp bài (Repeat Mode)
    if (c == 'r' || c == 'R') {
        repeat_mode_t rep = mp3_player_get_repeat_mode();
        rep = (repeat_mode_t)((rep + 1) % 3);
        mp3_player_set_repeat_mode(rep);
        printf(">>> [BÀN PHÍM]: ĐỔI CHẾ ĐỘ LẶP -> %s <<<\r\n", mp3_player_get_repeat_mode_name());
        led_effects_color_burst(255, 0, 255);
        return true;
    }

    // 6. Phím 's' hoặc 'S': Bật / Tắt Shuffle (Phát ngẫu nhiên)
    if (c == 's' || c == 'S') {
        repeat_mode_t rep = mp3_player_get_repeat_mode();
        if (rep == REPEAT_MODE_SHUFFLE) {
            mp3_player_set_repeat_mode(REPEAT_MODE_ALL);
            printf(">>> [BÀN PHÍM]: TẮT PHÁT NGẪU NHIÊN -> PHÁT TUẦN TỰ TOÀN BỘ <<<\r\n");
            led_effects_color_burst(0, 255, 120);
        } else {
            mp3_player_set_repeat_mode(REPEAT_MODE_SHUFFLE);
            printf(">>> [BÀN PHÍM]: BẬT PHÁT NGẪU NHIÊN (SHUFFLE MODE) <<<\r\n");
            led_effects_square_spin(true, 255, 100, 255);
        }
        return true;
    }

    // 7. Phím 'v' hoặc 'V': Đổi Visualizer LED nháy theo nhạc
    if (c == 'v' || c == 'V') {
        led_style_t st = led_effects_next_style();
        printf(">>> [BÀN PHÍM]: ĐỔI VISUALIZER LED -> %s <<<\r\n", led_effects_get_style_name(st));
        audio_play_led_mode_feedback((uint8_t)st);
        return true;
    }

    // 8. Phím 'm' hoặc 'M': Mute / Unmute nhanh
    if (c == 'm' || c == 'M') {
        bool muted = audio_synth_toggle_mute();
        printf(">>> [BÀN PHÍM]: %s <<<\r\n", muted ? "TẮT TIẾNG (MUTED)" : "BẬT TIẾNG (UNMUTED)");
        audio_play_mute_feedback(muted);
        return true;
    }

    // 9. Phím '+' / '=': Tăng âm lượng
    if (c == '+' || c == '=') {
        audio_synth_volume_up();
        float vol = audio_synth_get_volume();
        printf(">>> [BÀN PHÍM]: TĂNG ÂM LƯỢNG -> %.0f%% <<<\r\n", vol * 100.0f);
        audio_play_volume_tick(vol, true);
        led_effects_volume(vol);
        return true;
    }

    // 10. Phím '-' / '_': Giảm âm lượng
    if (c == '-' || c == '_') {
        audio_synth_volume_down();
        float vol = audio_synth_get_volume();
        printf(">>> [BÀN PHÍM]: GIẢM ÂM LƯỢNG -> %.0f%% <<<\r\n", vol * 100.0f);
        audio_play_volume_tick(vol, false);
        led_effects_volume(vol);
        return true;
    }

    // 11. Phím 'e' hoặc 'E': Bật / Tắt bộ lọc Hi-Fi DSP (Subsonic HPF + Bass Boost + Vocal Clarity)
    if (c == 'e' || c == 'E') {
        bool dsp = !audio_dsp_is_enabled();
        audio_dsp_set_enabled(dsp);
        printf("\r\n>>> [BÀN PHÍM]: BỘ LỌC HI-FI DSP (SUBSONIC + BASS + TREBLE) -> %s <<<\r\n",
               dsp ? "BẬT (BASS ẤM & RÕ TIẾNG)" : "TẮT (NGUYÊN BẢN)");
        led_effects_color_burst(dsp ? 0 : 255, dsp ? 255 : 100, 0);
        return true;
    }

    // Phím khác: nhấp nháy ánh sao
    led_effects_spark();
    return true;
}

static void mode_usb_player_background_tick(uint32_t delta_ms) {
    uint16_t cur_idx = mp3_player_get_current_index();
    mp3_player_state_t cur_st = mp3_player_get_state();

    if (cur_idx != s_last_reported_index || cur_st != s_last_reported_state) {
        s_last_reported_index = cur_idx;
        s_last_reported_state = cur_st;

        if (cur_st == MP3_STATE_PLAYING && mp3_player_get_total_tracks() > 0) {
            printf("\r\n>>> [ĐANG PHÁT: BÀI %d/%d]: %s <<<\r\n",
                   cur_idx + 1, mp3_player_get_total_tracks(), mp3_player_get_current_filename());
        }
    }
}

const toy_mode_driver_t g_mode_usb_player_driver = {
    .id                 = 9,
    .name               = "Máy Phát Nhạc USB MP3",
    .short_desc         = "Cắm USB nghe nhạc | BTN 6: Play/Pause | Đúp: Next | Giữ: Prev",
    .init               = mode_usb_player_init,
    .on_enter           = mode_usb_player_on_enter,
    .on_exit            = mode_usb_player_on_exit,
    .handle_key         = mode_usb_player_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_usb_player_background_tick,
    .handle_action      = mode_usb_player_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
