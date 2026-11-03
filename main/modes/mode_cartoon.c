#include <stdio.h>
#include <stdbool.h>
#include "esp_timer.h"
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

// Biến trạng thái nội bộ của Mode 6
static uint8_t s_cartoon_scenario_step = 0;
static uint16_t s_cartoon_combo = 0;
static int64_t s_cartoon_last_key_time = 0;

static void mode_cartoon_on_enter(void) {
    s_cartoon_scenario_step = 0;
    s_cartoon_combo = 0;
    printf("\r\n>>> [VŨ TRỤ GAME & PHIM HOẠT HÌNH: 34 ÂM THANH KINH ĐIỂN] <<<\r\n");
    printf(">>> [1..0/Numpad = Mario & Arcade | QWERTY = Action Hero | ASDFGH = Tom & Jerry Slapstick | ZXCVBNM = Phép Thuật] <<<\r\n");
    printf(">>> [Phím Space = Vở kịch rượt đuổi Tom & Jerry | Backspace = Troll Trombone | Enter = Fanfare Chiến Thắng] <<<\r\n");
    printf(">>> [Gõ phím liên tục tạo COMBO x5, COMBO x10 nhận phần thưởng âm thanh & đèn LED rực rỡ!] <<<\r\n\r\n");
}

static void check_and_schedule_combo_reward(void) {
    if (s_cartoon_combo == 5) {
        // Thưởng âm thanh 1-UP nối tiếp ngay sau tiếng của phím vừa bấm (delay 240ms)
        audio_play_cartoon_sfx_ex(SFX_GAME_1UP, 0, 240);
        led_effects_color_burst(255, 215, 0);
        printf(" -> [COMBO X5! SIÊU CHIẾN BINH HOẠT HÌNH - TẶNG THƯỞNG 1-UP!]\r\n");
    } else if (s_cartoon_combo >= 10) {
        audio_play_cartoon_sfx_ex(SFX_VICTORY_FANFARE, 0, 260);
        led_effects_celebration();
        printf(" -> [COMBO X10 HOÀN HẢO! BẬC THẦY GAME ARCADE - PHÁO HOA CHIẾN THẮNG!]\r\n");
        s_cartoon_combo = 0;
    }
}

static bool mode_cartoon_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    int64_t now = esp_timer_get_time();
    if (now - s_cartoon_last_key_time < 1200000LL) {
        s_cartoon_combo++;
    } else {
        s_cartoon_combo = 1;
    }
    s_cartoon_last_key_time = now;

    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 1. Phím SPACE: VỞ KỊCH HOẠT HÌNH TOM & JERRY (CARTOON CHASE DRAMA)
    if (c == ' ' || kc == 0x2C) {
        static const struct {
            cartoon_sfx_t sfx;
            const char *desc;
            uint8_t r, g, b;
        } s_chase_steps[] = {
            { SFX_CT_BONGO_RUN,       "[KỊCH 1: CHÂN GUỒNG TÍT MÙ BẮT ĐẦU RƯỢT ĐUỔI! (BONGO FEET)]", 255, 180, 0 },
            { SFX_GAME_SONIC_DASH,    "[KỊCH 2: PHÓNG VÈO BIẾN MẤT TỐC ĐỘ SIÊU THANH! (SONIC DASH)]", 0, 160, 255 },
            { SFX_CT_SLIP_FALL,       "[KỊCH 3: TRƯỢT VỎ CHUỐI TÉ NGÃ ĐAU ĐIẾNG! (BANANA SLIP & CRASH)]", 255, 120, 0 },
            { SFX_CT_ANVIL_CLANG,     "[KỊCH 4: CÁI ĐE SẮT RƠI TRÚNG ĐẦU BENGGG! (ANVIL CLANG)]", 255, 255, 255 },
            { SFX_CT_FLEXATONE,       "[KỊCH 5: CHOÁNG VÁNG SAO BAY VÒNG QUANH ĐẦU! (FLEXATONE)]", 50, 255, 100 },
            { SFX_GAME_OVER_TROMBONE, "[KỊCH 6: THẢM BẠI HÀI HƯỚC WAH-WAH-WAAAH! (SAD TROMBONE)]", 220, 40, 40 },
            { SFX_SPRING_BOING,       "[KỊCH 7: BẬT LÒ XO NHẢY DỰNG DẬY TƯNG TƯNG! (SPRING BOING)]", 50, 255, 50 },
            { SFX_CAR_HORN,           "[KỊCH 8: BẤM CÒI XE BIM BIM TẨU THOÁT! (CAR HORN)]", 255, 200, 0 }
        };
        uint8_t step = s_cartoon_scenario_step % 8;
        s_cartoon_scenario_step++;
        audio_play_cartoon_sfx(s_chase_steps[step].sfx);
        led_effects_color_burst(s_chase_steps[step].r, s_chase_steps[step].g, s_chase_steps[step].b);
        printf(" -> %s (Combo: %d)\r\n", s_chase_steps[step].desc, s_cartoon_combo);
        check_and_schedule_combo_reward();
        return true;
    }

    // 2. Phím BACKSPACE: Troll Sad Trombone
    if (c == '\b' || kc == 0x2A || kc == 0x4C) { // Backspace / Delete
        audio_play_cartoon_sfx(SFX_GAME_OVER_TROMBONE);
        led_effects_color_burst(200, 30, 30);
        printf(" -> [HOẠT HÌNH: TÉ NGÃ THẢM BẠI WAH WAH WAAAH! (SAD TROMBONE)]\r\n");
        check_and_schedule_combo_reward();
        return true;
    }

    // 3. Phím ENTER: Victory Fanfare
    if (c == '\r' || c == '\n' || kc == 0x28 || kc == 0x58) {
        audio_play_cartoon_sfx(SFX_VICTORY_FANFARE);
        led_effects_celebration();
        printf(" -> [CHIẾN THẮNG KHẢI HOÀN: LEVEL CLEAR FANFARE TƯNG BỪNG!]\r\n");
        s_cartoon_combo = 0;
        return true;
    }

    // 4. Phím TAB: Giới thiệu các vùng âm thanh
    if (kc == 0x2B) {
        audio_play_cartoon_sfx(SFX_CT_MAGIC_WAND);
        led_effects_color_burst(180, 50, 255);
        printf(" -> [VŨ TRỤ ÂM THANH: 1..0=Arcade | Q..P=Hero | A..L=Slapstick | Z..M=Magic]\r\n");
        check_and_schedule_combo_reward();
        return true;
    }

    // 5. Phím Mũi Tên (Arrow Keys)
    if (kc == 0x52) { // Arrow UP: High Jump
        audio_play_cartoon_sfx(SFX_GAME_JUMP);
        led_effects_color_burst(50, 255, 50);
        printf(" -> [MŨI TÊN LÊN: NHẢY BỔNG HIGH JUMP!]\r\n");
        check_and_schedule_combo_reward();
        return true;
    } else if (kc == 0x51) { // Arrow DOWN: Timpani Boom / Ground Pound
        audio_play_cartoon_sfx(SFX_CT_TIMPANI_BOOM);
        led_effects_color_burst(255, 50, 0);
        printf(" -> [MŨI TÊN XUỐNG: TIẾNG ĐẬP ĐẤT TIMPANI BOOM!]\r\n");
        check_and_schedule_combo_reward();
        return true;
    } else if (kc == 0x50) { // Arrow LEFT: Phanh gấp
        audio_play_cartoon_sfx(SFX_SKID_STOP);
        led_effects_color_burst(255, 100, 0);
        printf(" -> [MŨI TÊN TRÁI: PHANH XE CHÁY ĐƯỜNG KÉÉÉT!]\r\n");
        check_and_schedule_combo_reward();
        return true;
    } else if (kc == 0x4F) { // Arrow RIGHT: Phóng vù
        audio_play_cartoon_sfx(SFX_CT_RUN_AWAY);
        led_effects_color_burst(0, 200, 255);
        printf(" -> [MŨI TÊN PHẢI: PHÓNG VÈO BIẾN MẤT VÙÙÙ!]\r\n");
        check_and_schedule_combo_reward();
        return true;
    }

    // 6. Hàng Số & Numpad: RETRO ARCADE & MARIO
    if (c == '1' || kc == 0x59) { audio_play_cartoon_sfx(SFX_MARIO_COIN); led_effects_color_burst(255, 215, 0); printf(" -> [GAME 1: MARIO ĂN TIỀN VÀNG TING KENG!]\r\n"); }
    else if (c == '2' || kc == 0x5A) { audio_play_cartoon_sfx(SFX_GAME_JUMP); led_effects_color_burst(50, 255, 100); printf(" -> [GAME 2: MARIO NHẢY TƯNG BỔNG BOING-UÝT!]\r\n"); }
    else if (c == '3' || kc == 0x5B) { audio_play_cartoon_sfx(SFX_GAME_POWERUP); led_effects_color_burst(255, 50, 255); printf(" -> [GAME 3: ĂN NẤM BIẾN HÌNH KHỔNG LỒ POWER-UP!]\r\n"); }
    else if (c == '4' || kc == 0x5C) { audio_play_cartoon_sfx(SFX_GAME_1UP); led_effects_color_burst(0, 255, 200); printf(" -> [GAME 4: THÊM MẠNG 1-UP CỨU SINH!]\r\n"); }
    else if (c == '5' || kc == 0x5D) { audio_play_cartoon_sfx(SFX_GAME_FIREBALL); led_effects_color_burst(255, 80, 0); printf(" -> [GAME 5: BẮN CẦU LỬA PIU-CHÍU FIREBALL!]\r\n"); }
    else if (c == '6' || kc == 0x5E) { audio_play_cartoon_sfx(SFX_GAME_WARP); led_effects_color_burst(0, 150, 255); printf(" -> [GAME 6: CHUI ỐNG CỐNG WARP PIPE TÚT TÚT!]\r\n"); }
    else if (c == '7' || kc == 0x5F) { audio_play_cartoon_sfx(SFX_LASER); led_effects_color_burst(255, 30, 30); printf(" -> [GAME 7: SÚNG LASER PHI THUYỀN VŨ TRỤ PIU PIU!]\r\n"); }
    else if (c == '8' || kc == 0x60) { audio_play_cartoon_sfx(SFX_GAME_BOMB); led_effects_color_burst(255, 255, 255); printf(" -> [GAME 8: BOM NỔ 8-BIT BÙM RẦM!]\r\n"); }
    else if (c == '9' || kc == 0x61) { audio_play_cartoon_sfx(SFX_GAME_PACMAN); led_effects_color_burst(255, 255, 0); printf(" -> [GAME 9: PAC-MAN ĂN HẠT WAKA-WAKA!]\r\n"); }
    else if (c == '0' || kc == 0x62) { audio_play_cartoon_sfx(SFX_GAME_SONIC_DASH); led_effects_color_burst(0, 100, 255); printf(" -> [GAME 0: SONIC XOAY TÍT SPIN-DASH!]\r\n"); }
    else if (c == '-' || c == '_' || kc == 0x56) { audio_play_cartoon_sfx(SFX_GAME_PUNCH); led_effects_color_burst(255, 0, 0); printf(" -> [GAME [-]: CÚ ĐẤM ĐỐI KHÁNG K.O. BỐP!]\r\n"); }
    else if (c == '=' || c == '+' || kc == 0x57) { audio_play_cartoon_sfx(SFX_VICTORY_FANFARE); led_effects_celebration(); printf(" -> [GAME [+]: NHẠC QUA MÀN CHIẾN THẮNG FANFARE!]\r\n"); }

    // 7. Hàng chữ QWERTY: SIÊU ANH HÙNG & HÀNH ĐỘNG (Biến tấu cao độ độc lập không trùng hàng số!)
    else if (upper == 'Q') { audio_play_cartoon_sfx_ex(SFX_GAME_JUMP, 4, 0); led_effects_color_burst(50, 255, 100); printf(" -> [Q: ĐẠI NHẢY SIÊU ANH HÙNG DOUBLE-JUMP!]\r\n"); }
    else if (upper == 'W') { audio_play_cartoon_sfx_ex(SFX_CT_RUN_AWAY, 2, 0); led_effects_color_burst(0, 150, 255); printf(" -> [W: TIA CHỚP PHÓNG VỤT SIÊU TỐC!]\r\n"); }
    else if (upper == 'E') { audio_play_cartoon_sfx_ex(SFX_LASER, -3, 0); led_effects_color_burst(255, 30, 30); printf(" -> [E: ĐẠI PHÁO PLASMA HẠNG NẶNG!]\r\n"); }
    else if (upper == 'R') { audio_play_cartoon_sfx_ex(SFX_GAME_FIREBALL, 3, 0); led_effects_color_burst(255, 80, 0); printf(" -> [R: LIÊN HOÀN CẦU LỬA SIÊU CẤP!]\r\n"); }
    else if (upper == 'T') { audio_play_cartoon_sfx_ex(SFX_CT_TIMPANI_BOOM, 0, 0); led_effects_color_burst(255, 120, 0); printf(" -> [T: TRỐNG ĐỊNH ÂM TIMPANI BOOOOM!]\r\n"); }
    else if (upper == 'Y') { audio_play_cartoon_sfx_ex(SFX_MARIO_COIN, 5, 0); led_effects_color_burst(255, 215, 0); printf(" -> [Y: ĂN KIM CƯƠNG BÁU VẬT TING-TING!]\r\n"); }
    else if (upper == 'U') { audio_play_cartoon_sfx_ex(SFX_GAME_WARP, -2, 0); led_effects_color_burst(255, 50, 255); printf(" -> [U: DỊCH CHUYỂN TỨC THỜI TELEPORT!]\r\n"); }
    else if (upper == 'I') { audio_play_cartoon_sfx_ex(SFX_CT_GHOST_BOO, 2, 0); led_effects_color_burst(0, 255, 200); printf(" -> [I: HỒN MA TINH NGHỊCH Ú ÒA!]\r\n"); }
    else if (upper == 'O') { audio_play_cartoon_sfx_ex(SFX_GAME_BOMB, -3, 0); led_effects_color_burst(255, 255, 255); printf(" -> [O: SIÊU BOM NGUYÊN TỬ NỔ RỀN VANG!]\r\n"); }
    else if (upper == 'P') { audio_play_cartoon_sfx_ex(SFX_GAME_PUNCH, 3, 0); led_effects_color_burst(255, 0, 0); printf(" -> [P: CÚ ĐẤM THƯỢNG ĐẲNG UPPERCUT!]\r\n"); }

    // 8. Hàng chữ ASDFGH: TOM & JERRY SLAPSTICK COMEDY
    else if (upper == 'A') { audio_play_cartoon_sfx(SFX_CT_BONGO_RUN); led_effects_color_burst(255, 180, 0); printf(" -> [A: GUỒNG CHÂN CHẠY TẠI CHỖ BONGO FEET!]\r\n"); }
    else if (upper == 'S') { audio_play_cartoon_sfx(SFX_CT_SLIP_FALL); led_effects_color_burst(255, 100, 0); printf(" -> [S: TRƯỢT VỎ CHUỐI TÉ NGÃ ĐAU ĐIẾNG!]\r\n"); }
    else if (upper == 'D') { audio_play_cartoon_sfx(SFX_CT_ANVIL_CLANG); led_effects_color_burst(255, 255, 255); printf(" -> [D: ĐE SẮT RƠI TRÚNG ĐẦU BENG BENG!]\r\n"); }
    else if (upper == 'F') { audio_play_cartoon_sfx(SFX_CT_SLAPSTICK); led_effects_color_burst(255, 200, 50); printf(" -> [F: TÁT GỖ SLAPSTICK RẮC RẮC HÀI HƯỚC!]\r\n"); }
    else if (upper == 'G') { audio_play_cartoon_sfx(SFX_CT_FLEXATONE); led_effects_color_burst(50, 255, 100); printf(" -> [G: RUNG KIM LOẠI ẢO DIỆU FLEXATONE BOING-BOING!]\r\n"); }
    else if (upper == 'H') { audio_play_cartoon_sfx(SFX_CT_VIBRASLAP); led_effects_color_burst(200, 150, 50); printf(" -> [H: RUNG HÀM RĂNG LỪA VIBRASLAP CỜ-RẮC!]\r\n"); }
    else if (upper == 'J') { audio_play_cartoon_sfx(SFX_SPRING_BOING); led_effects_color_burst(50, 255, 50); printf(" -> [J: LÒ XO NHẢY TƯNG TƯNG BOING BOING!]\r\n"); }
    else if (upper == 'K') { audio_play_cartoon_sfx(SFX_CT_SQUEAK_DUCK); led_effects_color_burst(255, 220, 0); printf(" -> [K: BÓP VỊT CAO SU CHÍP CHÍP ĐÁNG YÊU!]\r\n"); }
    else if (upper == 'L') { audio_play_cartoon_sfx(SFX_CT_SPLAT); led_effects_color_burst(255, 100, 200); printf(" -> [L: BÁNH KEM ỤP MẶT SPLAT BẸP DÍ!]\r\n"); }

    // 9. Hàng chữ ZXCVBNM: PHÉP THUẬT & HIỆU ỨNG ĐẶC BIỆT
    else if (upper == 'Z') { audio_play_cartoon_sfx(SFX_CT_MAGIC_WAND); led_effects_color_burst(200, 50, 255); printf(" -> [Z: ĐŨA THẦN BÀ TIÊN LẤP LÁNH TING TING!]\r\n"); }
    else if (upper == 'X') { audio_play_cartoon_sfx(SFX_WATER_DROP); led_effects_color_burst(30, 150, 255); printf(" -> [X: GIỌT NƯỚC TÍ TÁCH RƠI TONG TONG!]\r\n"); }
    else if (upper == 'C') { audio_play_cartoon_sfx(SFX_CAR_HORN); led_effects_color_burst(255, 160, 0); printf(" -> [C: CÒI XE Ô TÔ HOẠT HÌNH BIM BIM!]\r\n"); }
    else if (upper == 'V') { audio_play_cartoon_sfx(SFX_FIRE_SIREN); led_effects_color_burst(255, 30, 30); printf(" -> [V: CÒI HỤ HOẠT HÌNH ACME SIREN!]\r\n"); }
    else if (upper == 'B') { audio_play_cartoon_sfx(SFX_CT_COWBELL); led_effects_color_burst(255, 180, 50); printf(" -> [B: CHUÔNG BÒ GÕ VUI NHỘN COWBELL!]\r\n"); }
    else if (upper == 'N') { audio_play_cartoon_sfx(SFX_CT_BALL_WHISTLE); led_effects_color_burst(0, 220, 255); printf(" -> [N: CÒI TRỌNG TÀI TUÝT TUÝT GẤP GÁP!]\r\n"); }
    else if (upper == 'M') { audio_play_cartoon_sfx(SFX_DOORBELL); led_effects_color_burst(200, 50, 255); printf(" -> [M: CHUÔNG CỬA ĐING ĐOONG NGÂN NGA!]\r\n"); }

    // 10. Phím Ký Tự, Dấu Câu & Phím Khác
    else if (c == '`' || c == '~') { audio_play_cartoon_sfx(SFX_CT_GHOST_BOO); led_effects_color_burst(100, 255, 200); printf(" -> [~: MA HOẠT HÌNH HÙ BÉ BOOO... HIHI!]\r\n"); }
    else if (c == '[' || c == '{') { audio_play_cartoon_sfx(SFX_CT_RUN_AWAY); led_effects_color_burst(0, 200, 255); printf(" -> [[: PHÓNG VÈO BIẾN MẤT VÙÙÙ!]\r\n"); }
    else if (c == ']' || c == '}') { audio_play_cartoon_sfx(SFX_SKID_STOP); led_effects_color_burst(255, 120, 0); printf(" -> []: PHANH GẤP KÉÉÉT!]\r\n"); }
    else if (c == ';' || c == ':') { audio_play_cartoon_sfx(SFX_CT_SQUEAK_DUCK); led_effects_color_burst(255, 220, 0); printf(" -> [;: VỊT CAO SU CHÍP CHÍP!]\r\n"); }
    else if (c == '\'' || c == '"') { audio_play_cartoon_sfx(SFX_CT_MAGIC_WAND); led_effects_color_burst(200, 50, 255); printf(" -> [': PHÉP THUẬT LẤP LÁNH!]\r\n"); }
    else if (c == ',' || c == '<') { audio_play_cartoon_sfx(SFX_CT_FLEXATONE); led_effects_color_burst(50, 255, 100); printf(" -> [<: RUNG KIM LOẠI ẢO DIỆU BOING BOING!]\r\n"); }
    else if (c == '.' || c == '>') { audio_play_cartoon_sfx(SFX_CT_ANVIL_CLANG); led_effects_color_burst(255, 255, 255); printf(" -> [>: ĐE SẮT RƠI TRÚNG ĐẦU BENG!]\r\n"); }
    else if (c == '/' || c == '?') { audio_play_cartoon_sfx(SFX_WHISTLE_SLIDE); led_effects_color_burst(0, 200, 255); printf(" -> [?: TIẾNG CÒI TÀU HOẠT HÌNH TU TU!]\r\n"); }
    else {
        cartoon_sfx_t sfx_id = (cartoon_sfx_t)(10 + (kc % 24));
        audio_play_cartoon_sfx(sfx_id);
        led_effects_spark();
        printf(" -> [CARTOON: HIỆU ỨNG ÂM THANH HOẠT HÌNH (Combo: %d)]\r\n", s_cartoon_combo);
    }

    check_and_schedule_combo_reward();
    return true;
}

// Cấu hình Driver cho Chế độ 6
const toy_mode_driver_t g_mode_cartoon_driver = {
    .id                 = 5,
    .name               = "6. HỘP ÂM THANH GAME & HOẠT HÌNH",
    .short_desc         = "34 hiệu ứng âm thanh Tom & Jerry, Mario, súng laser, combo gõ phím nhận thưởng",
    .init               = NULL,
    .on_enter           = mode_cartoon_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_cartoon_handle_key,
    .handle_key_release = NULL,
    .background_tick    = NULL,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
