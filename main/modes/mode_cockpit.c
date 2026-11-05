#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_timer.h"
#include "mode_interface.h"
#include "audio_synth.h"
#include "led_effects.h"

// Bảng dữ liệu 26 loài động vật (Chuẩn hóa 100% tên tiếng Anh đúng chữ cái đầu A-Z)
typedef struct {
    char key;
    const char *name_vi;
    const char *name_en;
    const char *action;
    uint8_t r, g, b;
} animal_info_t;

static const animal_info_t s_animals[26] = {
    {'A', "Cá Sấu",           "Alligator",   "Đớp mồi ngoạm ngoạm trong đầm lầy!",    40, 180,  40},
    {'B', "Chim Rừng",         "Bird",        "Hót líu lo ríu rít chuyền cành!",        0, 190, 255},
    {'C', "Mèo Con",           "Cat",         "Kêu meo meo nũng nịu đòi ăn!",         255, 180,  80},
    {'D', "Chú Chó",           "Dog",         "Sủa gâu gâu vẫy đuôi mừng rỡ!",        220, 140,  40},
    {'E', "Voi Con",           "Elephant",    "Thổi kèn vòi rống vang dội!",          160, 170, 190},
    {'F', "Chú Ếch",           "Frog",        "Ộp ộp nhảy tõm xuống ao sen!",          50, 230,  50},
    {'G', "Dê Núi",            "Goat",        "Kêu be be leo núi đá dựng đứng!",      230, 230, 210},
    {'H', "Ngựa Chiến",        "Horse",       "Hí vang phi nước đại lộc cộc!",        180,  90,  30},
    {'I', "Côn Trùng Dế Mèn",  "Insect",      "Gáy ri ri du dương đêm hè!",            30, 200, 100},
    {'J', "Báo Gấm",           "Jaguar",      "Gầm gừ rình mồi trong rừng sâu!",      255, 150,   0},
    {'K', "Gấu Koala",         "Koala",       "Ôm cành bạch đàn kêu khẹc khẹc!",      150, 150, 160},
    {'L', "Sư Tử",             "Lion",        "Chúa sơn lâm gầm vang rúng động!",     255, 200,   0},
    {'M', "Chú Khỉ",           "Monkey",      "Chí chóe nhảy nhót trêu đùa!",         190, 120,  50},
    {'N', "Chim Sơn Ca",       "Nightingale", "Hót véo von thánh thót giữa rừng!",     30, 220, 180},
    {'O', "Cú Mèo",            "Owl",         "Gù gù canh gác đêm trăng tròn!",       140,  80, 200},
    {'P', "Heo Hồng",          "Pig",         "Ủn ỉn ụt ịt tắm bùn vui vẻ!",          255, 130, 170},
    {'Q', "Vịt Quack",         "Quack Duck",  "Cạp cạp bơi lội xòe đôi cánh!",        255, 220,  20},
    {'R', "Gà Trống",          "Rooster",     "Gáy ò ó o gọi mặt trời thức giấc!",    240,  40,  40},
    {'S', "Cừu Trắng",         "Sheep",       "Be be gặm cỏ non trên thảo nguyên!",   240, 240, 240},
    {'T', "Cọp Hổ",            "Tiger",       "Oai phong lẫm liệt gầm chúa rừng!",    255,  80,   0},
    {'U', "Kỳ Lân",            "Unicorn",     "Phép thuật lung linh hào quang!",      200, 130, 255},
    {'V', "Chim Ưng Kền Kền",  "Vulture",     "Vỗ cánh thét rền lao vút từ mây!",     200,  80,  50},
    {'W', "Chó Sói",           "Wolf",        "Hú trăng huuu hoang dã đại ngàn!",     130, 150, 180},
    {'X', "Báo Săn Tia Chớp",  "X-Cheetah",   "Bứt tốc chạy nhanh nhất thế giới!",    240, 160,  30},
    {'Y', "Bò Tây Tạng",       "Yak",         "Thở phì phò gầm rền trên tuyết trắng!", 160, 100, 60},
    {'Z', "Ngựa Vằn",          "Zebra",       "Sọc đen trắng phi lách cách!",         200, 200, 220}
};

static bool s_rescue_mission_active = false;
static char s_rescue_target_animal = 0;
static uint16_t s_rescue_score = 0;
static int64_t s_rescue_next_prompt_time = 0;
static bool s_police_toggle = false;
static uint8_t s_police_tick_div = 0;
static uint32_t s_siren_active_ms = 0;

static void mode_cockpit_on_enter(void) {
    s_rescue_mission_active = false;
    s_rescue_target_animal = 0;
    s_rescue_score = 0;
    s_rescue_next_prompt_time = 0;
    s_police_toggle = false;
    s_police_tick_div = 0;
    s_siren_active_ms = 0;

    printf("\r\n>>> [BUỒNG LÁI XE CỨU HỎA ĐẶC NHIỆM & 26 CON VẬT (FULL A-Z)] <<<\r\n");
    printf(">>> [Phím A..Z: 26 Bạn Thú Rừng Thực Tế (100%% không trùng phím xe)] <<<\r\n");
    printf(">>> [Phím Space: Đạp Ga Vroom Vroom (hoặc Nghe Lại Đề Bài khi đang Cứu Hộ)] <<<\r\n");
    printf(">>> [Enter: Vòi Rồng Phun Nước | Backspace: Phanh Két Két | Tab/Caps: Bật/Tắt Còi Ưu Tiên 114] <<<\r\n");
    printf(">>> [Mũi tên: Lên=Tăng tốc | Xuống=Số lùi bíp bíp | Trái/Phải=Xi-nhan rẽ] <<<\r\n");
    printf(">>> [Phím 1..0: 1=Còi hơi | 2=Bộ đàm 114 | 3=Chuông đồn | 4=Đèn pha | 5=Lửa cháy | 6=Trực thăng | 7=Hoan hô | 8=Turbo] <<<\r\n");
    printf(">>> [Phím 9 hoặc F10 / ~: BẬT MINI-GAME \"NHIỆM VỤ CỨU HỘ VƯỜN THÚ 114\"!] <<<\r\n\r\n");
}

static void mode_cockpit_on_exit(void) {
    if (audio_cockpit_is_siren_on()) {
        audio_cockpit_siren_set(false);
    }
    s_rescue_mission_active = false;
    s_siren_active_ms = 0;
}

static bool mode_cockpit_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // Reset bộ đếm tự tắt còi hụ khi bé còn đang tương tác
    s_siren_active_ms = 0;

    // 1. Phím chữ cái A - Z: 100% 26 Con vật thực tế (Không bị phím xe cứu hỏa đè)
    if (upper >= 'A' && upper <= 'Z') {
        int idx = upper - 'A';
        const animal_info_t *an = &s_animals[idx];
        audio_play_animal_sound(upper);
        led_effects_color_burst(an->r, an->g, an->b);

        if (s_rescue_mission_active) {
            if (upper == s_rescue_target_animal) {
                s_rescue_score += 100;
                printf("\r\n[NHIỆM VỤ CỨU HỘ 114: CHÍNH XÁC!]\r\n");
                printf(" -> Bé đã cứu thành công bạn: [%s (%s)]!\r\n", an->name_vi, an->name_en);
                printf(" -> [ĐIỂM CỨU HỘ: %u ĐIỂM] - Đang tìm bạn thú tiếp theo...\r\n\r\n", (unsigned int)s_rescue_score);
                // Trì hoãn giọng khen "Hoan hô đúng rồi" 900ms để bé nghe trọn vẹn tiếng con vật vừa cứu!
                audio_cockpit_play_sfx_delayed(COCKPIT_SFX_SUCCESS, 900);
                led_effects_rescue_success();

                // Chọn con vật tiếp theo ngẫu nhiên
                char next_a = 'A' + (rand() % 26);
                while (next_a == s_rescue_target_animal) next_a = 'A' + (rand() % 26);
                s_rescue_target_animal = next_a;
                s_rescue_next_prompt_time = esp_timer_get_time() + 3200000LL; // 3.2s sau phát câu đố mới
            } else {
                printf(" -> [114 NHẮC NHỞ]: Bé vừa bấm bạn [%s (%s)]. Hãy lắng nghe lại tiếng bạn thú cần cứu nhé!\r\n", an->name_vi, an->name_en);
                s_rescue_next_prompt_time = esp_timer_get_time() + 2000000LL; // 2.0s sau phát lại câu đố
            }
        } else {
            printf(" -> [VƯỜN THÚ 114: '%c'] %s (%s) - %s\r\n", upper, an->name_vi, an->name_en, an->action);
        }
        return true;
    }

    // 2. Phím Space: Chân ga xe cứu hỏa (Hoặc nghe lại tiếng thú cần cứu khi đang chơi Mini-Game!)
    if (c == ' ' || kc == 0x2C) {
        if (s_rescue_mission_active && s_rescue_target_animal >= 'A' && s_rescue_target_animal <= 'Z') {
            audio_play_animal_sound(s_rescue_target_animal);
            s_rescue_next_prompt_time = esp_timer_get_time() + 5500000LL;
            led_effects_searchlight();
            printf(" -> [114 GỢI Ý]: Phát lại tiếng bạn thú đang cần giải cứu! Đố bé là chữ gì?\r\n");
        } else {
            audio_cockpit_engine_start();
            led_effects_color_burst(255, 90, 0);
            printf(" -> [BUỒNG LÁI: NỔ MÁY RỀN VANG - VROOM VROOM!]\r\n");
        }
        return true;
    }
    // 3. Phím Enter: Vòi rồng phun nước dập lửa cực mạnh
    else if (c == '\r' || c == '\n' || kc == 0x28 || kc == 0x58) {
        audio_cockpit_water_hose();
        led_effects_water_spray();
        printf(" -> [BUỒNG LÁI: VÒI RỒNG PHUN NƯỚC XỐI XẢ DẬP LỬA!]\r\n");
        return true;
    }
    // 4. Phím Tab hoặc CapsLock: Bật/Tắt còi cứu hỏa ưu tiên liên tục
    else if (kc == 0x2B || kc == 0x39) { // Tab or CapsLock
        audio_cockpit_siren_toggle();
        s_siren_active_ms = 0;
        printf(" -> [BUỒNG LÁI: CÒI CỨU HỎA ƯU TIÊN 114 %s (ĐÈN XANH ĐỎ CHỚP AN TOÀN)!]\r\n",
               audio_cockpit_is_siren_on() ? "BẬT" : "TẮT");
        return true;
    }
    // 5. Phím Backspace: Phanh gấp cháy đường
    else if (c == '\b' || kc == 0x2A || kc == 0x4C) {
        audio_cockpit_skid();
        led_effects_color_burst(255, 40, 0);
        printf(" -> [BUỒNG LÁI: PHANH XE CHÁY ĐƯỜNG KÉÉÉT!]\r\n");
        return true;
    }
    // 6. 4 Phím Mũi tên: Điều hướng bánh lái
    else if (kc == 0x52) { // Arrow Up: Tăng tốc
        audio_cockpit_engine_start();
        led_effects_color_burst(255, 140, 0);
        printf(" -> [BÁNH LÁI: TĂNG TỐC VƯỢT GIAO LỘ!]\r\n");
        return true;
    }
    else if (kc == 0x51) { // Arrow Down: Số lùi bíp bíp
        audio_cockpit_reverse();
        led_effects_color_burst(255, 200, 0);
        printf(" -> [BÁNH LÁI: GÀI SỐ LÙI AN TOÀN (BÍP... BÍP...)]\r\n");
        return true;
    }
    else if (kc == 0x50) { // Arrow Left: Xi-nhan rẽ trái
        audio_cockpit_turn();
        led_effects_turn_signal(true);
        printf(" -> [BÁNH LÁI: XI-NHAN RẼ TRÁI (TẠCH TẠCH)!]\r\n");
        return true;
    }
    else if (kc == 0x4F) { // Arrow Right: Xi-nhan rẽ phải
        audio_cockpit_turn();
        led_effects_turn_signal(false);
        printf(" -> [BÁNH LÁI: XI-NHAN RẼ PHẢI (TẠCH TẠCH)!]\r\n");
        return true;
    }
    // 7. Hàng phím số (1..9, 0) & Numpad: Bảng điều khiển đặc nhiệm 114
    else if (c == '1' || kc == 0x59) {
        audio_cockpit_horn();
        led_effects_color_burst(255, 170, 0);
        printf(" -> [BẢNG ĐIỀU KHIỂN: CÒI HƠI XE TẢI CỨU HỎA (TUUUT TUUUT)!]\r\n");
        return true;
    }
    else if (c == '2' || kc == 0x5A) {
        audio_cockpit_radio();
        led_effects_color_burst(0, 200, 255);
        printf(" -> [BẢNG ĐIỀU KHIỂN: BỘ ĐÀM 114: \"BÁO ĐỘNG 114, XE XUẤT KÍCH!\"]\r\n");
        return true;
    }
    else if (c == '3' || kc == 0x5B) {
        audio_cockpit_bell();
        led_effects_color_burst(255, 220, 0);
        printf(" -> [BẢNG ĐIỀU KHIỂN: CHUÔNG BÁO CHÁY ĐỒN CỨU HỎA (KENG KENG)!]\r\n");
        return true;
    }
    else if (c == '4' || kc == 0x5C) {
        led_effects_searchlight();
        audio_cockpit_play_sfx(COCKPIT_SFX_TURN);
        printf(" -> [BẢNG ĐIỀU KHIỂN: BẬT ĐÈN PHA CHIẾU SÁNG XUYÊN KHÓI LỬA!]\r\n");
        return true;
    }
    else if (c == '5' || kc == 0x5D) {
        audio_cockpit_fire();
        led_effects_fire_glow();
        printf(" -> [BẢNG ĐIỀU KHIỂN: TIẾNG ĐÁM CHÁY TÍ TÁCH BẬP BÙNG!]\r\n");
        return true;
    }
    else if (c == '6' || kc == 0x5E) {
        audio_cockpit_helicopter();
        led_effects_color_burst(100, 200, 255);
        printf(" -> [BẢNG ĐIỀU KHIỂN: TRỰC THĂNG CỨU HỘ DẬP LỬA TRÊN KHÔNG!]\r\n");
        return true;
    }
    else if (c == '7' || kc == 0x5F) {
        audio_cockpit_cheer();
        led_effects_rescue_success();
        printf(" -> [BẢNG ĐIỀU KHIỂN: TIẾNG VỖ TAY REO HÒ CẢM ƠN CHIẾN SĨ 114!]\r\n");
        return true;
    }
    else if (c == '8' || kc == 0x60) {
        audio_cockpit_rocket_launch();
        led_effects_color_burst(255, 0, 0);
        printf(" -> [BẢNG ĐIỀU KHIỂN: NITRO TURBO PHÓNG NHANH CỨU NẠN KHẨN CẤP!]\r\n");
        return true;
    }
    else if (c == '9' || kc == 0x61 || kc == 0x43 || c == '`' || c == '~') { // Phím 9 hoặc F10 / ~: Mini-game
        s_rescue_mission_active = !s_rescue_mission_active;
        if (s_rescue_mission_active) {
            s_rescue_score = 0;
            s_rescue_target_animal = 'A' + (rand() % 26);
            audio_cockpit_play_sfx(COCKPIT_SFX_MISSION);
            s_rescue_next_prompt_time = esp_timer_get_time() + 2400000LL; // 2.4s
            led_effects_rescue_success();
            printf("\r\n========================================================\r\n");
            printf(">>> [BẬT NHIỆM VỤ CỨU HỘ VƯỜN THÚ 114] <<<\r\n");
            printf(">>> Hãy lắng nghe tiếng kêu phát ra từ loa...\r\n");
            printf(">>> Đố bé biết đó là bạn thú nào? Bấm phím chữ cái (A-Z) để giải cứu nhé!\r\n");
            printf(">>> (Bấm phím SPACE bất kỳ lúc nào để nghe lại tiếng bạn thú)\r\n");
            printf("========================================================\r\n\r\n");
        } else {
            printf("\r\n>>> [TẮT NHIỆM VỤ CỨU HỘ - TRỞ VỀ LÁI TỰ DO] <<<\r\n\r\n");
        }
        return true;
    }
    else if (c == '0' || kc == 0x62) {
        audio_play_cartoon_sfx(SFX_VICTORY_FANFARE);
        led_effects_rescue_success();
        printf(" -> [BẢNG ĐIỀU KHIỂN: KHÚC QUÂN HÀNH CHIẾN THẮNG DẬP TẮT LỬA!]\r\n");
        return true;
    }
    else {
        audio_cockpit_horn();
        led_effects_color_burst(255, 160, 0);
        printf(" -> [BUỒNG LÁI: BẤM NÚT ĐẶC CHỦNG 114!]\r\n");
        return true;
    }
}

static void mode_cockpit_background_tick(uint32_t delta_ms) {
    if (audio_cockpit_is_siren_on()) {
        s_siren_active_ms += delta_ms;
        // Tự động tắt còi hụ sau 15 giây không thao tác để bảo vệ thính giác & không gây ồn khi bé rời đi
        if (s_siren_active_ms >= 15000) {
            audio_cockpit_siren_set(false);
            s_siren_active_ms = 0;
            led_effects_clear();
            printf(" -> [BUỒNG LÁI: TỰ ĐỘNG TẮT CÒI ƯU TIÊN SAU 15 GIÂY ĐỂ BẢO VỆ THÍNH GIÁC BÉ]\r\n");
        } else {
            // Đảo đèn xanh đỏ mỗi 250ms (2Hz) êm dịu và an toàn cho mắt trẻ nhỏ
            s_police_tick_div++;
            if (s_police_tick_div >= 5) {
                s_police_tick_div = 0;
                s_police_toggle = !s_police_toggle;
                led_effects_police(s_police_toggle);
            }
        }
    }

    if (s_rescue_mission_active && s_rescue_next_prompt_time > 0) {
        int64_t now = esp_timer_get_time();
        if (now >= s_rescue_next_prompt_time) {
            audio_play_animal_sound(s_rescue_target_animal);
            // Tự động nhắc lại tiếng thú sau mỗi 5.5 giây nếu bé chưa bấm trả lời
            s_rescue_next_prompt_time = now + 5500000LL;
        }
    }
}

static void mode_cockpit_handle_action(void) {
    audio_cockpit_siren_toggle();
    bool on = audio_cockpit_is_siren_on();
    printf("\r\n>>> [BTN 6 ACTION: CÒI HÚ CỨU HỎA 114: %s] <<<\r\n", on ? "BẬT CÒI ƯU TIÊN" : "TẮT CÒI");
    fflush(stdout);
    if (!on) {
        audio_play_robot_chirp();
    }
    led_effects_police(on);
}

// Cấu hình Driver cho Chế độ 8
const toy_mode_driver_t g_mode_cockpit_driver = {
    .id                 = 7,
    .name               = "8. BUỒNG LÁI XE CỨU HỎA ĐẶC NHIỆM & 26 CON VẬT CỨU HỘ (FULL A-Z)",
    .short_desc         = "Buồng lái xe cứu hỏa 114 (chân ga, vòi rồng, còi ưu tiên) & mini-game cứu hộ 26 loài thú",
    .init               = NULL,
    .on_enter           = mode_cockpit_on_enter,
    .on_exit            = mode_cockpit_on_exit,
    .handle_key         = mode_cockpit_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_cockpit_background_tick,
    .handle_action      = mode_cockpit_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
