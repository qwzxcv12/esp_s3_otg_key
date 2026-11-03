#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

static void mode_speed_on_enter(void) {
    audio_speed_reset();
    speed_note_info_t info;
    audio_speed_get_info(&info);

    printf("\r\n================================================================================\r\n");
    printf(">>> CHẾ ĐỘ 3: NHẠC CHẠY THEO TỐC ĐỘ GÕ PHÍM <<<\r\n");
    printf("================================================================================\r\n");
    printf(" -> BÉ GÕ BẤT KỲ PHÍM NÀO ĐỀU TẤU NỐT NHẠC TIẾP THEO CỦA BÀI HÁT!\r\n");
    printf(" -> Gõ chậm rãi: Nhạc ngân dài, du dương, êm đềm.\r\n");
    printf(" -> Gõ dồn dập: Nhạc giòn giã, rộn rã, tưng bừng theo nhịp gõ của tay!\r\n");
    printf(" -> Bài mở đầu (%d/%d): \"%s\" (%s)\r\n",
           (int)(info.song_idx + 1), (int)info.total_songs, info.song_name, info.title_vi);
    printf(" -> Phím Mũi Tên: Đổi bài | Phím TAB: Đổi nhạc cụ (%s) | Phím 1..5: Chọn bài\r\n",
           audio_speed_get_timbre_name());
    printf("================================================================================\r\n\r\n");

    led_effects_color_burst(0, 180, 255);
}

static void mode_speed_on_exit(void) {
    audio_speed_reset();
    led_effects_clear();
}

static bool mode_speed_handle_key(uint8_t kc, uint8_t mod, char c) {
    // 1. Phím Mũi tên Phải: Đổi sang bài tiếp theo
    if (kc == 0x4F) {
        audio_speed_next_song();
        speed_note_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [BÀI TIẾP THEO (%d/%d): \"%s\" (%s)] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.song_name, info.title_vi);
        led_effects_mode_switch(2);
        return true;
    }

    // 2. Phím Mũi tên Trái: Lùi về bài trước
    if (kc == 0x50) {
        audio_speed_prev_song();
        speed_note_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [LÙI BÀI TRƯỚC (%d/%d): \"%s\" (%s)] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.song_name, info.title_vi);
        led_effects_mode_switch(2);
        return true;
    }

    // 3. Phím TAB: Đổi âm sắc nhạc cụ (Piano / Chuông Ngân / 8-Bit)
    if (kc == 0x2B) {
        audio_speed_toggle_timbre();
        printf("\r\n>>> [ĐỔI NHẠC CỤ -> %s] <<<\r\n", audio_speed_get_timbre_name());
        led_effects_color_burst(255, 215, 0);
        return true;
    }

    // 4. Phím số 1..5: Chọn trực tiếp bài hát 1..5
    if (c >= '1' && c <= '5') {
        audio_speed_select_song((uint8_t)(c - '1'));
        speed_note_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [CHỌN BÀI SỐ %d: \"%s\" (%s)] <<<\r\n",
               (int)(c - '0'), info.song_name, info.title_vi);
        led_effects_mode_switch(2);
        return true;
    }

    // 5. MỌI PHÍM BẤM CÒN LẠI (Chữ, Số, Space, Enter, Backspace, v.v.):
    // Đều tấu nốt nhạc tiếp theo ngay lập tức (100% phản hồi tức thời)!
    speed_note_info_t info;
    bool finished = audio_speed_play_note(&info);

    // Hiệu ứng LED tỏa sáng theo nốt nhạc trên dải 30 bóng
    uint8_t r = 0, g = 200, b = 255;
    if (info.speed_bpm >= 200) {
        r = 255; g = 80; b = 40;  // Gõ rất nhanh: Đỏ cam rực lửa
    } else if (info.speed_bpm >= 130) {
        r = 255; g = 210; b = 0;  // Gõ vừa: Vàng ấm vui tươi
    }
    led_effects_note(info.note_idx, info.total_notes, r, g, b);

    if (finished) {
        printf(" -> [🎵 \"%s\" | Nốt cuối -> Lặp lại mượt mà! | Tốc độ: %u BPM]
",
               info.song_name, (unsigned int)info.speed_bpm);
    } else {
        printf(" -> [🎵 \"%s\" | Nốt %u/%u (%.1f Hz) | Tốc độ: %u BPM | %s]\r\n",
               info.song_name,
               (unsigned int)info.note_idx,
               (unsigned int)info.total_notes,
               info.freq,
               (unsigned int)info.speed_bpm,
               audio_speed_get_timbre_name());
    }
    return true;
}

static void mode_speed_background_tick(uint32_t delta_ms) {
    // Không làm gì gây rối khi bé dừng tay - Nhạc dừng theo tay người dùng
}

// Cấu hình Driver cho Chế độ 3
const toy_mode_driver_t g_mode_speed_driver = {
    .id                 = 2,
    .name               = "3. NHẠC CHẠY THEO TỐC ĐỘ GÕ PHÍM",
    .short_desc         = "Bé gõ bất kỳ phím nào cũng ra bài hát: gõ nhanh nhạc nhanh, gõ chậm nhạc chậm",
    .init               = NULL,
    .on_enter           = mode_speed_on_enter,
    .on_exit            = mode_speed_on_exit,
    .handle_key         = mode_speed_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_speed_background_tick,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
