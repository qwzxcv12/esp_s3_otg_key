#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

static uint8_t s_speed_led_level = 0;
static uint32_t s_idle_decay_ms = 0;
static uint16_t s_speed_high_score = 0; // Kỷ lục tốc độ cao nhất (High Score BPM)

static void mode_speed_on_enter(void) {
    audio_speed_reset();
    s_speed_led_level = 0;
    s_idle_decay_ms = 0;
    speed_note_info_t info;
    audio_speed_get_info(&info);

    printf("\r\n================================================================================\r\n");
    printf(">>> CHẾ ĐỘ 3: ĐƯỜNG ĐUA ÂM NHẠC F1 TRÊN KHUNG HÌNH VUÔNG (32 LED) <<<\r\n");
    printf("================================================================================\r\n");
    printf(" -> BÉ GÕ BẤT KỲ PHÍM NÀO ĐỀU TẤU NỐT NHẠC & ĐẠP GA XE ĐUA QUANH 4 CẠNH!\r\n");
    printf(" -> Gõ chậm rãi: Xe lướt nhẹ quanh khung vuông, nhạc ngân du dương.\r\n");
    printf(" -> Gõ dồn dập: Xe đua ôm cua 4 góc cực tít, đạt Nitro bốc lửa cầu vồng!\r\n");
    printf(" -> Bài mở đầu (%d/%d): \"%s\" (%s)\r\n",
           (int)(info.song_idx + 1), (int)info.total_songs, info.song_name, info.title_vi);
    printf(" -> Phím Mũi Tên: Đổi bài | Phím TAB/F10: Đổi nhạc cụ (%s)\r\n",
           audio_speed_get_timbre_name());
    printf("================================================================================\r\n\r\n");
}

static void mode_speed_on_exit(void) {
    audio_speed_reset();
    s_speed_led_level = 0;
    led_effects_clear();
}

static bool mode_speed_handle_key(uint8_t kc, uint8_t mod, char c) {
    bool has_ctrl_or_alt = ((mod & 0x55) != 0);

    // 1. Phím Mũi tên Phải hoặc PageDown: Đổi sang bài tiếp theo
    if (kc == 0x4F || kc == 0x4E) {
        audio_speed_next_song();
        speed_note_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [BÀI TIẾP THEO (%d/%d): \"%s\" (%s)] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.song_name, info.title_vi);
        led_effects_square_spin(true, 0, 220, 255);
        return true;
    }

    // 2. Phím Mũi tên Trái hoặc PageUp: Lùi về bài trước
    if (kc == 0x50 || kc == 0x4B) {
        audio_speed_prev_song();
        speed_note_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [LÙI BÀI TRƯỚC (%d/%d): \"%s\" (%s)] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.song_name, info.title_vi);
        led_effects_square_spin(false, 255, 160, 0);
        return true;
    }

    // 3. Phím TAB hoặc F10: Đổi âm sắc nhạc cụ (Piano / Chuông Ngân / 8-Bit)
    if (kc == 0x2B || kc == 0x43) {
        audio_speed_toggle_timbre();
        printf("\r\n>>> [ĐỔI NHẠC CỤ -> %s] <<<\r\n", audio_speed_get_timbre_name());
        led_effects_piano_chord(255, 215, 0);
        return true;
    }

    // 4. Tổ hợp Ctrl/Alt + Phím số 1..5: Chọn trực tiếp bài hát 1..5
    if (has_ctrl_or_alt && c >= '1' && c <= '5') {
        audio_speed_select_song((uint8_t)(c - '1'));
        speed_note_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [CHỌN BÀI SỐ %d: \"%s\" (%s)] <<<\r\n",
               (int)(c - '0'), info.song_name, info.title_vi);
        led_effects_mode_switch(2);
        return true;
    }

    // 5. MỌI PHÍM BẤM CÒN LẠI: Tấu nốt nhạc tiếp theo & tăng tốc xe đua quanh khung hình vuông 32 LED!
    speed_note_info_t info;
    bool finished = audio_speed_play_note(&info);

    float ratio = (float)(info.speed_bpm - 60) / 220.0f;
    if (ratio < 0.08f) ratio = 0.08f;
    if (ratio > 1.0f)  ratio = 1.0f;
    uint8_t target_leds = (uint8_t)(ratio * (float)LED_STRIP_COUNT + 0.5f);
    if (target_leds < 2) target_leds = 2;
    if (target_leds > LED_STRIP_COUNT) target_leds = LED_STRIP_COUNT;
    s_speed_led_level = target_leds;
    s_idle_decay_ms = 0;

    // Phát hiện kỷ lục tốc độ mới (Top Speed Record)
    bool is_new_record = false;
    if (info.speed_bpm > s_speed_high_score && info.speed_bpm >= 120) {
        s_speed_high_score = info.speed_bpm;
        is_new_record = true;
    }

    // Hiệu ứng rít lốp bốc khói Drift khi ôm cua 4 góc ở tốc độ cao
    if (info.speed_bpm >= 150) {
        led_effects_speed_brake();
    }

    if (finished) {
        if (info.speed_bpm >= 150) {
            led_effects_celebration();
        } else {
            led_effects_speedometer(s_speed_led_level, ratio);
        }
        printf(" -> [\"%s\" | Hoàn thành vòng đua! | Tốc độ: %u BPM%s]\r\n",
               info.song_name, (unsigned int)info.speed_bpm,
               is_new_record ? " - KỶ LỤC MỚI!" : "");
    } else {
        if (info.speed_bpm >= 180) {
            led_effects_speed_nitro();
            printf(" -> [\"%s\" | NITRO SIÊU TỐC: %u BPM | KỶ LỤC ĐỈNH CAO: %u BPM!]\r\n",
                   info.song_name, (unsigned int)info.speed_bpm, s_speed_high_score);
        } else {
            led_effects_speedometer(s_speed_led_level, ratio);
            printf(" -> [\"%s\" | Nốt %u/%u (%.1f Hz) | Tốc độ: %u BPM%s | %s]\r\n",
                   info.song_name,
                   (unsigned int)info.note_idx,
                   (unsigned int)info.total_notes,
                   info.freq,
                   (unsigned int)info.speed_bpm,
                   is_new_record ? " (Kỷ lục mới!)" : "",
                   audio_speed_get_timbre_name());
        }
    }
    return true;
}

static void mode_speed_background_tick(uint32_t delta_ms) {
    if (s_speed_led_level > 0) {
        s_idle_decay_ms += delta_ms;
        if (s_idle_decay_ms >= 350) {
            s_idle_decay_ms -= 120;
            if (s_speed_led_level > 2) {
                s_speed_led_level -= 2;
            } else {
                s_speed_led_level = 0;
            }
            float ratio = (float)s_speed_led_level / (float)LED_STRIP_COUNT;
            led_effects_speedometer(s_speed_led_level, ratio);
        }
    }
}

static void mode_speed_handle_action(void) {
    audio_speed_next_song();
    speed_note_info_t info;
    audio_speed_get_info(&info);
    printf("\r\n>>> [BTN 6 ACTION: SPEED ĐỔI BÀI HÁT: \"%s\" (%s)] <<<\r\n",
           info.song_name, info.title_vi ? info.title_vi : "");
    fflush(stdout);
    audio_play_song_change_chime();
    audio_play_piano_note_dur(info.freq, TIMBRE_PIANO, 0, 350);
    led_effects_square_spin(true, 255, 120, 0);
}

const toy_mode_driver_t g_mode_speed_driver = {
    .id                 = 2,
    .name               = "3. NHẠC CHẠY THEO TỐC ĐỘ GÕ PHÍM",
    .short_desc         = "Đường đua F1 trên khung hình vuông 32 LED: gõ càng nhanh xe chạy quanh 4 cạnh càng tít",
    .init               = NULL,
    .on_enter           = mode_speed_on_enter,
    .on_exit            = mode_speed_on_exit,
    .handle_key         = mode_speed_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_speed_background_tick,
    .handle_action      = mode_speed_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
