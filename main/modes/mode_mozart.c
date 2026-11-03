#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

static void mode_mozart_on_enter(void) {
    mozart_play_info_t info;
    audio_mozart_get_info(&info);
    printf("\r\n>>> [BABY MOZART READY: 20 Tuyệt Phẩm Cổ Điển & Thiếu Nhi] <<<\r\n");
    printf(">>> [Bài mở đầu: \"%s\" - %s (%d nốt)] <<<\r\n", info.name, info.artist, (int)info.total_notes);
    printf(">>> [Phím tắt: Space/Mũi tên = Đổi bài | Tab/T = Đổi nhạc cụ | Enter/P = Hát Ru Auto-Play | 1..9 = Chọn bài] <<<\r\n\r\n");
}

static void mode_mozart_on_exit(void) {
    audio_mozart_set_autoplay(false);
}

static bool mode_mozart_handle_key(uint8_t kc, uint8_t mod, char c) {
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 1. Phím Enter hoặc 'P': Bật / Tắt chế độ Auto-Play (Hát ru / Demo)
    if (kc == 0x28 || kc == 0x58 || upper == 'P') {
        audio_mozart_toggle_autoplay();
        bool ap = audio_mozart_is_autoplay();
        printf("\r\n>>> [BABY MOZART: %s CHẾ ĐỘ TỰ ĐỘNG BIỂU DIỄN (LULLABY AUTO-PLAY)] <<<\r\n",
               ap ? "BẬT" : "TẮT");
        led_effects_color_burst(ap ? 0 : 255, ap ? 200 : 100, ap ? 255 : 50);
        return true;
    }

    // Tắt auto-play nếu bé bắt đầu tự tay gõ phím
    if (audio_mozart_is_autoplay()) {
        audio_mozart_set_autoplay(false);
        printf(" -> [BABY MOZART: Bé bắt đầu tự tay gõ -> Tạm dừng tự động!]\r\n");
    }

    // 2. Phím Space hoặc Mũi tên Phải: Bài kế tiếp
    if (c == ' ' || kc == 0x4F) {
        audio_mozart_next_song();
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART - BÀI HÁT KẾ TIẾP (%d/%d): \"%s\" - %s (%d nốt)] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.name, info.artist, (int)info.total_notes);
        led_effects_mode_switch(1);
        return true;
    }
    // 3. Phím Mũi tên Trái: Lùi bài trước
    else if (kc == 0x50) {
        audio_mozart_prev_song();
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART - BÀI TRƯỚC (%d/%d): \"%s\" - %s (%d nốt)] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.name, info.artist, (int)info.total_notes);
        led_effects_mode_switch(1);
        return true;
    }
    // 4. Phím Tab hoặc 'T': Đổi âm sắc nhạc cụ
    else if (kc == 0x2B || upper == 'T') {
        audio_mozart_toggle_timbre();
        printf("\r\n>>> [BABY MOZART: ĐỔI NHẠC CỤ -> %s] <<<\r\n", audio_mozart_get_timbre_name());
        audio_play_cartoon_sfx(SFX_MARIO_COIN);
        led_effects_color_burst(255, 215, 0);
        return true;
    }
    // 5. Phím 'R': Chơi lại bài hiện tại từ đầu
    else if (upper == 'R') {
        audio_mozart_restart_song();
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART: CHƠI LẠI TỪ ĐẦU BÀI: \"%s\"] <<<\r\n", info.name);
        led_effects_color_burst(100, 255, 100);
        return true;
    }
    // 6. Phím số 1..9 và 0: Chọn trực tiếp bài hát 1..10
    else if (c >= '1' && c <= '9') {
        uint8_t target = (c - '1');
        audio_mozart_select_song(target);
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART - CHỌN BÀI %d/%d: \"%s\" - %s] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.name, info.artist);
        led_effects_mode_switch(1);
        return true;
    }
    else if (c == '0') {
        audio_mozart_select_song(9); // Bài số 10
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART - CHỌN BÀI 10/%d: \"%s\" - %s] <<<\r\n",
               (int)info.total_songs, info.name, info.artist);
        led_effects_mode_switch(1);
        return true;
    }
    // 7. Tất cả các phím còn lại: Bé chơi nốt tiếp theo (One-Key-One-Note)!
    else {
        mozart_play_info_t info;
        bool finished = audio_mozart_play_next_note(&info);
        led_effects_mozart_note(info.freq, info.bass_freq, info.timbre);

        if (finished) {
            audio_play_praise();
            led_effects_celebration();
            printf(" -> [XUẤT SẮC! BÉ VỪA HOÀN THÀNH BÀI: \"%s\" (%s)!]\r\n",
                   info.name, info.artist);
        } else {
            printf(" -> [BABY MOZART: \"%s\" | Nốt %d/%d: %.1f Hz %s| Đàn %s]\r\n",
                   info.name, (int)info.note_idx, (int)info.total_notes, info.freq,
                   (info.bass_freq > 20.0f) ? "+ Bass hòa thanh " : "",
                   audio_mozart_get_timbre_name());
        }
        return true;
    }
}

static void mode_mozart_background_tick(uint32_t delta_ms) {
    audio_mozart_autoplay_tick();
}

// Cấu hình Driver cho Chế độ 2
const toy_mode_driver_t g_mode_mozart_driver = {
    .id                 = 1,
    .name               = "2. NGHỆ SĨ THIÊN TÀI (BABY MOZART)",
    .short_desc         = "20 tuyệt phẩm cổ điển, bấm phím bất kỳ để gảy từng nốt hoặc tự động hát ru",
    .init               = NULL,
    .on_enter           = mode_mozart_on_enter,
    .on_exit            = mode_mozart_on_exit,
    .handle_key         = mode_mozart_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_mozart_background_tick,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
