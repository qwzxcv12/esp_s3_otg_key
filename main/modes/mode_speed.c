#include <stdio.h>
#include <stdbool.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

static const char *s_level_names[] = {
    "🛑 ĐỖ XE / DỪNG BÁNH",
    "🐢 XE RÙA CHẬM RÃI (60-90 BPM)",
    "🚗 XE ĐUA THỂ THAO (100-140 BPM)",
    "🏎️ SIÊU XE F1 RỰC LỬA (150-200 BPM)",
    "🚀 NITRO TÊN LỬA VŨ TRỤ (220-280+ BPM)"
};

static void print_speed_banner(void) {
    speed_play_info_t info;
    audio_speed_get_info(&info);

    printf("\r\n================================================================================\r\n");
    printf("🏎️🏎️🏎️  CHẾ ĐỘ 3: ĐƯỜNG ĐUA ÂM NHẠC THẦN TỐC (SONIC SPEED RUNNER)  🏎️🏎️🏎️\r\n");
    printf("================================================================================\r\n");
    printf(" [!] Bàn phím là BÀN ĐẠP GA SIÊU XE! Gõ càng nhanh, bài hát chạy càng dồn dập!\r\n");
    printf(" [!] Dải LED 30 mắt biến thành ĐỒNG HỒ TỐC ĐỘ XE ĐUA (Speedometer 0 - 300 Km/h)!\r\n");
    printf(" >>> Đường đua hiện tại (%d/%d): \"%s\" (%s)\r\n",
           (int)(info.track_idx + 1), (int)info.total_tracks, info.name, info.theme);
    printf(" >>> Chế độ chơi: [%s] | Nhạc cụ: [%s]\r\n\r\n",
           audio_speed_is_auto_pedal() ? "CHÂN GA TỰ ĐỘNG (Bé gõ giữ nhịp)" : "GÕ NỐT TIẾT TẤU (Từng nốt nảy lửa)",
           audio_speed_get_timbre_name());
    printf(" >>> HƯỚNG DẪN CÁC PHÍM TƯƠNG TÁC SIÊU VUI:\r\n");
    printf("   - GÕ BẤT KỲ PHÍM NÀO: Đạp ga tăng tốc! Gõ liên thanh để bứt tốc Tên Lửa!\r\n");
    printf("   - Phím SPACE / SHIFT: Bơm NITRO BOOST tăng tốc 280 Km/h xé gió!\r\n");
    printf("   - Phím C / H / ENTER : Bấm còi xe BIM BIM xin đường (Honk Honk)!\r\n");
    printf("   - Phím BACKSPACE / B : Phanh gấp KÉÉÉT dừng xe an toàn!\r\n");
    printf("   - Phím MŨI TÊN       : Phải = Bài tiếp | Trái = Bài trước | Xuống = Phanh!\r\n");
    printf("   - Phím 1 .. 5        : Chọn trực tiếp 5 Đường Đua thiếu nhi quen thuộc\r\n");
    printf("   - Phím TAB / T       : Đổi Nhạc Cụ (Đàn 8-Bit Mario / Piano / Chuông Ngân)\r\n");
    printf("   - Phím M             : Đổi [A: Đạp Ga Tự Động] <-> [B: Gõ Nốt Tiết Tấu]\r\n");
    printf("================================================================================\r\n\r\n");
}

static void mode_speed_on_enter(void) {
    audio_speed_engine_init();
    print_speed_banner();
    led_effects_speedometer(0, 0.0f);
}

static void mode_speed_on_exit(void) {
    audio_speed_engine_reset();
    led_effects_clear();
}

static bool mode_speed_handle_key(uint8_t kc, uint8_t mod, char c) {
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 1. Phím Còi Xe Bim Bim: 'c', 'h', Enter (kc 0x28, 0x58)
    if (upper == 'C' || upper == 'H' || c == '\r' || c == '\n' || kc == 0x28 || kc == 0x58) {
        audio_speed_honk();
        led_effects_color_burst(255, 200, 0);
        printf("\r\n🚗 [CÒI XE: BIM BIM! TRÁNH ĐƯỜNG CHO TAY ĐUA NHÍ NÀO!] 🚗\r\n");
        return true;
    }

    // 2. Phím Phanh Gấp Khẩn Cấp: Backspace (kc 0x2A / '\b'), 'B', Mũi tên Xuống (kc 0x51)
    if (c == '\b' || upper == 'B' || kc == 0x51) {
        audio_speed_brake();
        led_effects_speed_brake();
        printf("\r\n🛑 [PHANH GẤP KÉÉÉT! Đèn phanh đỏ rực! Xe dừng lại an toàn!] 🛑\r\n");
        return true;
    }

    // 3. Phím Bơm Nitro Tên Lửa: Space, Shift (mod & 0x22), 'Z'
    if (c == ' ' || upper == 'Z') {
        audio_speed_nitro_boost();
        led_effects_speed_nitro();
        printf("\r\n🚀🚀🚀 [NITRO TÊN LỬA VŨ TRỤ KÍCH HOẠT! TỐC ĐỘ 280 KM/H! XÉ GIÓ PHÓNG VÚT!] 🚀🚀🚀\r\n");
        return true;
    }

    // 4. Phím 'M': Đổi giữa 2 cơ chế chơi (Auto Cruise Pedal <-> Rhythm Step Runner)
    if (upper == 'M') {
        bool ap = !audio_speed_is_auto_pedal();
        audio_speed_set_auto_pedal(ap);
        audio_play_cartoon_sfx(SFX_MARIO_COIN);
        led_effects_color_burst(100, 255, 100);
        printf("\r\n>>> [ĐỔI CHẾ ĐỘ CHƠI: %s] <<<\r\n",
               ap ? "CHÂN GA TỰ ĐỘNG (Bé gõ đều tay nạp ga, bài hát hát mượt mà)"
                  : "GÕ NỐT TIẾT TẤU (Mỗi phím gõ phát ngay 1 nốt bài hát)");
        return true;
    }

    // 5. Phím Tab hoặc 'T': Đổi âm sắc nhạc cụ
    if (kc == 0x2B || upper == 'T') {
        audio_speed_toggle_timbre();
        printf("\r\n>>> [ĐỔI ÂM SẮC NHẠC CỤ -> %s] <<<\r\n", audio_speed_get_timbre_name());
        led_effects_color_burst(255, 215, 0);
        return true;
    }

    // 6. Phím Mũi tên Phải: Đổi đường đua tiếp theo
    if (kc == 0x4F) {
        audio_speed_next_track();
        speed_play_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [CHUYỂN ĐƯỜNG ĐUA TIẾP THEO (%d/%d): \"%s\" (%s)] <<<\r\n",
               (int)(info.track_idx + 1), (int)info.total_tracks, info.name, info.theme);
        led_effects_mode_switch(2);
        return true;
    }

    // 7. Phím Mũi tên Trái: Lùi về đường đua trước
    if (kc == 0x50) {
        audio_speed_prev_track();
        speed_play_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [LÙI VỀ ĐƯỜNG ĐUA TRƯỚC (%d/%d): \"%s\" (%s)] <<<\r\n",
               (int)(info.track_idx + 1), (int)info.total_tracks, info.name, info.theme);
        led_effects_mode_switch(2);
        return true;
    }

    // 8. Phím 'R': Chạy lại từ đầu vạch xuất phát
    if (upper == 'R') {
        audio_speed_restart_track();
        speed_play_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [CHẠY LẠI TỪ ĐẦU ĐƯỜNG ĐUA: \"%s\"] <<<\r\n", info.name);
        led_effects_color_burst(80, 220, 255);
        return true;
    }

    // 9. Phím số 1..5: Chọn trực tiếp đường đua 1..5
    if (c >= '1' && c <= '5') {
        audio_speed_select_track((uint8_t)(c - '1'));
        speed_play_info_t info;
        audio_speed_get_info(&info);
        printf("\r\n>>> [CHỌN ĐƯỜNG ĐUA SỐ %d: \"%s\" (%s)] <<<\r\n",
               (int)(c - '0'), info.name, info.theme);
        led_effects_mode_switch(2);
        return true;
    }

    // 10. TẤT CẢ CÁC PHÍM CÒN LẠI: ĐẠP GA TĂNG TỐC!
    if (audio_speed_is_auto_pedal()) {
        // Chế độ A: Đạp ga tự động
        audio_speed_tap_pedal();
        speed_play_info_t info;
        audio_speed_get_info(&info);
        led_effects_speedometer(info.speed_level, info.current_speed_kmh / 280.0f);

        printf(" -> [🏎️ ĐẠP GA! %3.0f KM/H | %s | COMBO x%u %s| NỐT %u/%u]\r\n",
               info.current_speed_kmh,
               s_level_names[info.speed_level],
               (unsigned int)info.combo,
               (info.combo >= 25) ? "🔥🔥 " : (info.combo >= 10 ? "✨ " : ""),
               (unsigned int)info.note_idx,
               (unsigned int)info.total_notes);
    } else {
        // Chế độ B: Gõ từng nốt nảy lửa
        speed_play_info_t info;
        bool finished = audio_speed_step_note(&info);
        led_effects_speedometer(info.speed_level, info.current_speed_kmh / 280.0f);

        if (finished) {
            led_effects_celebration();
            printf("\r\n🎉🎉🎉 [XUẤT SẮC! BÉ VỪA VỀ ĐÍCH HOÀN THÀNH BÀI: \"%s\"!] 🎉🎉🎉\r\n\r\n", info.name);
        } else {
            printf(" -> [🎵 NỐT %u/%u | %3.0f KM/H | %s | COMBO x%u %s]\r\n",
                   (unsigned int)info.note_idx,
                   (unsigned int)info.total_notes,
                   info.current_speed_kmh,
                   s_level_names[info.speed_level],
                   (unsigned int)info.combo,
                   (info.combo >= 25) ? "🔥🔥 " : "");
        }
    }
    return true;
}

static void mode_speed_background_tick(uint32_t delta_ms) {
    speed_play_info_t info;
    bool note_played = false;
    bool finished = audio_speed_engine_tick(delta_ms, &info, &note_played);

    if (note_played) {
        led_effects_speedometer(info.speed_level, info.current_speed_kmh / 280.0f);
        if (finished) {
            led_effects_celebration();
            printf("\r\n🎉🎉🎉 [XUẤT SẮC! BÉ VỪA VỀ ĐÍCH HOÀN THÀNH BÀI: \"%s\"!] 🎉🎉🎉\r\n\r\n", info.name);
        }
    } else if (info.current_speed_kmh < 15.0f) {
        // Xe dừng lại: giữ đèn hậu đỏ nhẹ
        led_effects_speedometer(0, 0.0f);
    }
}

// Cấu hình Driver cho Chế độ 3
const toy_mode_driver_t g_mode_speed_driver = {
    .id                 = 2,
    .name               = "3. ĐƯỜNG ĐUA ÂM NHẠC THẦN TỐC (SONIC SPEED RUNNER)",
    .short_desc         = "Bàn phím là chân ga siêu xe, gõ càng nhanh nhạc chạy càng dồn dập, LED đo tốc độ 300 Km/h",
    .init               = NULL,
    .on_enter           = mode_speed_on_enter,
    .on_exit            = mode_speed_on_exit,
    .handle_key         = mode_speed_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_speed_background_tick,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
