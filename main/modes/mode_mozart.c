#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"
#include "esp_timer.h"

static int64_t s_last_key_press_us = 0;
static int64_t s_celebration_until_us = 0;
static bool s_need_song_intro = false;

static void mode_mozart_on_enter(void) {
    mozart_play_info_t info;
    audio_mozart_get_info(&info);
    s_last_key_press_us = 0;
    s_celebration_until_us = 0;
    s_need_song_intro = false;
    printf("\r\n>>> [BABY MOZART READY: 20 Tuyệt Phẩm Cổ Điển & Thiếu Nhi (Khung Vuông 4x8 LED)] <<<\r\n");
    printf(">>> [Bài mở đầu: \"%s\" - %s (%d nốt)] <<<\r\n", info.name, info.artist, (int)info.total_notes);
    printf(">>> [Phím tắt: Mũi tên Trái/Phải = Đổi bài | Tab/F10 = Đổi nhạc cụ | Enter = Hát Ru Auto-Play | Backspace = Chơi lại] <<<\r\n\r\n");
}

static void mode_mozart_on_exit(void) {
    audio_mozart_set_autoplay(false);
    s_celebration_until_us = 0;
    s_need_song_intro = false;
}

static bool mode_mozart_handle_key(uint8_t kc, uint8_t mod, char c) {
    bool has_ctrl_or_alt = ((mod & 0x55) != 0);

    // 1. Phím Enter hoặc Mũi tên Lên: Bật / Tắt chế độ Auto-Play (Vòng xoáy Cực Quang 360°)
    if (kc == 0x28 || kc == 0x58 || kc == 0x52) {
        s_celebration_until_us = 0;
        s_need_song_intro = false;
        audio_mozart_toggle_autoplay();
        bool ap = audio_mozart_is_autoplay();
        printf("\r\n>>> [BABY MOZART: %s CHẾ ĐỘ TỰ ĐỘNG BIỂU DIỄN (AURORA AUTO-PLAY)] <<<\r\n",
               ap ? "BẬT" : "TẮT");
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        led_effects_mozart_progress(info.freq, 0.0f, info.timbre, info.note_idx, info.total_notes, ap);
        return true;
    }

    // 2. Phím Mũi tên Phải hoặc PageDown: Bài kế tiếp (Xoay 1 vòng thuận chiều kim đồng hồ quanh khung vuông)
    if (kc == 0x4F || kc == 0x4E) {
        s_celebration_until_us = 0;
        s_need_song_intro = false;
        audio_mozart_next_song();
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART - BÀI HÁT KẾ TIẾP (%d/%d): \"%s\" - %s (%d nốt)] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.name, info.artist, (int)info.total_notes);
        led_effects_square_spin(true, 0, 220, 255);
        return true;
    }

    // 3. Phím Mũi tên Trái hoặc PageUp: Lùi bài trước (Xoay 1 vòng ngược chiều kim đồng hồ quanh khung vuông)
    if (kc == 0x50 || kc == 0x4B) {
        s_celebration_until_us = 0;
        s_need_song_intro = false;
        audio_mozart_prev_song();
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART - BÀI TRƯỚC (%d/%d): \"%s\" - %s (%d nốt)] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.name, info.artist, (int)info.total_notes);
        led_effects_square_spin(false, 255, 160, 0);
        return true;
    }

    // 4. Phím Tab hoặc F10: Đổi âm sắc nhạc cụ (Piano / Music Box / 8-bit)
    if (kc == 0x2B || kc == 0x43) {
        audio_mozart_toggle_timbre();
        printf("\r\n>>> [BABY MOZART: ĐỔI NHẠC CỤ -> %s] <<<\r\n", audio_mozart_get_timbre_name());
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        audio_play_piano_note_dur(523.25f, (piano_timbre_t)info.timbre, 0, 260);
        led_effects_piano_chord(255, 215, 0);
        return true;
    }

    // 5. Phím Backspace hoặc Home: Chơi lại bài hiện tại từ đầu (Tua ngược vòng sáng về 0)
    if (kc == 0x2A || kc == 0x4A) {
        s_celebration_until_us = 0;
        s_need_song_intro = false;
        audio_mozart_restart_song();
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART: CHƠI LẠI TỪ ĐẦU BÀI: \"%s\" (%d nốt)] <<<\r\n", info.name, (int)info.total_notes);
        led_effects_square_spin(false, 80, 255, 120);
        return true;
    }

    // 6. Tổ hợp Ctrl/Alt + Phím số 1..9 và 0: Chọn nhanh bài hát 1..10 cho phụ huynh
    if (has_ctrl_or_alt && ((c >= '1' && c <= '9') || c == '0')) {
        s_celebration_until_us = 0;
        s_need_song_intro = false;
        uint8_t target = (c == '0') ? 9 : (uint8_t)(c - '1');
        audio_mozart_select_song(target);
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART - CHỌN BÀI %d/%d: \"%s\" - %s (%d nốt)] <<<\r\n",
               (int)(info.song_idx + 1), (int)info.total_songs, info.name, info.artist, (int)info.total_notes);
        led_effects_mode_switch(target);
        return true;
    }

    // 7. Tất cả các phím còn lại: Gảy từng nốt nhạc + Sao chổi + 4 góc Bass + Vòng tiến độ 32 LED
    int64_t now_us = esp_timer_get_time();

    // Chống rung phím và chống bấm nhầm phím kép (< 65ms)
    int64_t diff_us = now_us - s_last_key_press_us;
    if (diff_us < 65000LL) {
        return true;
    }
    s_last_key_press_us = now_us;

    // Đo tốc độ gõ biểu cảm (Dynamic Tempo): Allegro sôi động vs Andante êm dịu
    const char *tempo_tag = "Moderato";
    if (diff_us < 220000LL) {
        tempo_tag = "Allegro (Sôi động)";
    } else if (diff_us > 550000LL) {
        tempo_tag = "Andante (Êm dịu)";
    }

    // Nếu vừa hoàn thành bài và đang phát "Bé giỏi quá!" + hiệu ứng pháo hoa ăn mừng (1.2 giây):
    // Các phím bấm theo quán tính của bé trong 1.2s này sẽ không ăn lẹm vào nốt đầu bài tiếp theo!
    if (now_us < s_celebration_until_us) {
        return true;
    }

    if (audio_mozart_is_autoplay()) {
        audio_mozart_set_autoplay(false);
        printf(" -> [BABY MOZART: Bé bắt đầu tự tay gõ -> Chuyển sang chế độ Bé Tự Biểu Diễn!]\r\n");
    }

    if (s_need_song_intro) {
        s_need_song_intro = false;
        mozart_play_info_t next_info;
        audio_mozart_get_info(&next_info);
        printf("\r\n>>> [BÉ BẮT ĐẦU BÀI TIẾP THEO (%d/%d): \"%s\" - %s (%d nốt)] <<<\r\n",
               (int)(next_info.song_idx + 1), (int)next_info.total_songs,
               next_info.name, next_info.artist, (int)next_info.total_notes);
    }

    mozart_play_info_t info;
    bool finished = audio_mozart_play_next_note(&info);
    led_effects_mozart_progress(info.freq, info.bass_freq, info.timbre, info.note_idx, info.total_notes, false);

    if (finished) {
        s_celebration_until_us = now_us + 1200000LL; // Khung giờ vàng 1.2s ăn mừng
        s_need_song_intro = true;
        audio_play_praise();
        led_effects_celebration();
        printf(" -> [XUẤT SẮC! BÉ VỪA HOÀN THÀNH TRỌN VẸN BÀI: \"%s\" (%s) - %d nốt!]\r\n",
               info.name, info.artist, (int)info.total_notes);
    } else {
        printf(" -> [BABY MOZART: \"%s\" | Nốt %d/%d: %.1f Hz %s| Nhịp %s | Đàn %s]\r\n",
               info.name, (int)info.note_idx, (int)info.total_notes, info.freq,
               (info.bass_freq > 20.0f) ? "+ Bass 4 Góc " : "",
               tempo_tag,
               audio_mozart_get_timbre_name());
    }
    return true;
}

static uint32_t s_mozart_hint_timer_ms = 0;

static void mode_mozart_background_tick(uint32_t delta_ms) {
    audio_mozart_autoplay_tick();

    // Chế độ Follow-the-Light: Khi bé tự đánh, đèn LED ở vị trí nốt tiếp theo nhấp nháy dẫn đường
    if (!audio_mozart_is_autoplay()) {
        s_mozart_hint_timer_ms += delta_ms;
        if (s_mozart_hint_timer_ms >= 500) {
            s_mozart_hint_timer_ms = 0;
            mozart_play_info_t info;
            audio_mozart_get_info(&info);
            if (info.total_notes > 0 && info.note_idx < info.total_notes) {
                uint8_t hint_led = (uint8_t)((info.note_idx * (LED_STRIP_COUNT - 1)) / info.total_notes);
                led_effects_note(hint_led, LED_STRIP_COUNT, 255, 215, 0);
            }
        }
    }
}

static void mode_mozart_handle_action(void) {
    s_celebration_until_us = 0;
    s_need_song_intro = false;
    audio_mozart_next_song();
    mozart_play_info_t info;
    audio_mozart_get_info(&info);
    printf("\r\n>>> [BTN 6 ACTION: MOZART ĐỔI BÀI HÁT: \"%s\" (%s) - %d nốt] <<<\r\n",
           info.name, info.artist, (int)info.total_notes);
    fflush(stdout);
    audio_play_song_change_chime();
    audio_play_piano_note_dur(info.freq, audio_mozart_get_timbre(), 0, 350);
    led_effects_square_spin(true, 0, 200, 255);
}

const toy_mode_driver_t g_mode_mozart_driver = {
    .id                 = 1,
    .name               = "2. NGHỆ SĨ THIÊN TÀI (BABY MOZART)",
    .short_desc         = "20 tuyệt phẩm cổ điển, sao chổi giao hưởng, 4 góc Bass và vòng cực quang hát ru",
    .init               = NULL,
    .on_enter           = mode_mozart_on_enter,
    .on_exit            = mode_mozart_on_exit,
    .handle_key         = mode_mozart_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_mozart_background_tick,
    .handle_action      = mode_mozart_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
