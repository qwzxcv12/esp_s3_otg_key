#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"

static bool s_quiz_active = false;
static char s_quiz_target = 'A';
static char s_prev_letter = 0;

// Trạng thái Trò chơi "Bé Tập Làm Toán Thần Tốc" (Math Genius Quiz)
typedef enum {
    MATH_STATE_IDLE = 0,
    MATH_STATE_SPEAK_A,
    MATH_STATE_SPEAK_OP,
    MATH_STATE_SPEAK_B,
    MATH_STATE_SPEAK_EQUAL,
    MATH_STATE_WAIT_INPUT,
    MATH_STATE_CELEBRATE
} math_quiz_state_t;

static bool s_math_active = false;
static math_quiz_state_t s_math_state = MATH_STATE_IDLE;
static uint8_t s_math_a = 0;
static uint8_t s_math_b = 0;
static uint8_t s_math_op = 0; // 0 = Cộng (+), 1 = Trừ (-)
static int8_t s_math_target = 0;
static int8_t s_math_user_ans = -1;
static uint32_t s_math_timer_ms = 0;
static uint32_t s_math_pulse_timer = 0;
static bool s_math_pulse_toggle = false;
static uint16_t s_math_score = 0;
static uint8_t s_math_streak = 0;

static void math_quiz_start_question(void) {
    s_math_active = true;
    s_quiz_active = false; // Tắt quiz chữ nếu đang mở

    s_math_op = (rand() % 2); // 0 = +, 1 = -
    if (s_math_op == 0) {
        // Phép cộng (+): A + B <= 10
        s_math_a = 1 + (rand() % 5);               // 1..5
        s_math_b = 1 + (rand() % (10 - s_math_a)); // 1..(10 - A)
        s_math_target = (int8_t)(s_math_a + s_math_b);
    } else {
        // Phép trừ (-): A - B >= 0
        s_math_a = 2 + (rand() % 9);               // 2..10
        s_math_b = 1 + (rand() % s_math_a);        // 1..A
        s_math_target = (int8_t)(s_math_a - s_math_b);
    }

    s_math_user_ans = -1;
    s_math_state = MATH_STATE_SPEAK_A;
    s_math_timer_ms = 100;

    printf("\r\n============================================================\r\n");
    printf(">>> [TRÒ CHƠI: BÉ TẬP LÀM TOÁN THẦN TỐC - CÂU ĐỐ SỐ %d] <<<\r\n", (s_math_score / 10) + 1);
    printf("           >>> [   %d  %c  %d  =  ?   ] <<<\r\n",
           s_math_a, (s_math_op == 0 ? '+' : '-'), s_math_b);
    printf("------------------------------------------------------------\r\n");
    printf(" - Cạnh Trái (Cyan) : %d bóng đèn số hạng thứ nhất\r\n", s_math_a);
    printf(" - Cạnh Phải (Cam)  : %d bóng đèn số hạng thứ hai\r\n", s_math_b);
    printf(" - Cạnh Trên        : Dấu [%c] (%s)\r\n",
           (s_math_op == 0 ? '+' : '-'), (s_math_op == 0 ? "Vàng - Phép Cộng" : "Đỏ - Phép Trừ"));
    printf(" - Cạnh Dưới        : Chờ bé bấm phím số kết quả (0..10)!\r\n");
    printf(" - Phím SPACE / '?' : Nghe lại câu hỏi | Phím ESC: Thoát trò chơi\r\n");
    printf("============================================================\r\n\r\n");
    fflush(stdout);

    led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, -1, true);
}

static const char *s_spell_dict[26] = {
    "Chữ A -> A... Con Cá / Quả Cam!",
    "Chữ B (Bờ) -> Quả Bóng / Con Bò!",
    "Chữ C (Cờ) -> Con Cò / Cái Cốc!",
    "Chữ D (Dờ) -> Con Dê / Quả Dưa Hấu!",
    "Chữ E (E) -> Em Bé / Quả Me!",
    "Chữ F (Ép) -> Fansipan / Phích Nước (Frog - Chú Ếch)!",
    "Chữ G (Gờ) -> Con Gà Trống / Cái Ghế!",
    "Chữ H (Hờ) -> Bông Hoa Đẹp / Con Hổ!",
    "Chữ I (I) -> Viên Bi Tròn / Con Khỉ!",
    "Chữ J (Gi) -> Judo / Jaguar (Báo Đốm)!",
    "Chữ K (Ca) -> Cây Kem Mát Lạnh / Cái Kéo!",
    "Chữ L (Lờ) -> Chiếc Lá Xanh / Quả Lê!",
    "Chữ M (Mờ) -> Mặt Trời / Con Mèo Con!",
    "Chữ N (Nờ) -> chiếc Nón Lá / Quả Na!",
    "Chữ O (O) -> Con Ong Chăm Chỉ / Quả Bóng Tròn!",
    "Chữ P (Pờ) -> Đèn Pin Sáng / Phố Phường!",
    "Chữ Q (Quy) -> Quả Quýt Ngọt / Cây Quạt Mát!",
    "Chữ R (Rờ) -> Con Rùa Chậm Chạp / Rừng Cây!",
    "Chữ S (Sờ) -> Chú Sóc Nâu / Ngôi Sao Sáng!",
    "Chữ T (Tờ) -> Đoàn Tàu Hỏa / Quả Táo Đỏ!",
    "Chữ U (U) -> Quả Đu Đủ / Chiếc Mũ Xinh!",
    "Chữ V (Vờ) -> Chú Voi Con / Con Vịt Vàng!",
    "Chữ W (Vê-Kép) -> Wolf (Chó Sói) / Web Toàn Cầu!",
    "Chữ X (Xờ) -> Chiếc Xe Đạp / Quả Xoài Ngọt!",
    "Chữ Y (I-Dài) -> Chim Yến / Yêu Thương Bố Mẹ!",
    "Chữ Z (Dét) -> Zebra (Ngựa Vằn) / Đường Zíc-Zắc!"
};

static void mode_phonics_on_enter(void) {
    s_math_active = false;
    s_math_state = MATH_STATE_IDLE;
    s_quiz_active = false;
    s_prev_letter = 0;

    printf("\r\n>>> [BÉ HỌC ĐÁNH VẦN CHỮ CÁI, TẬP ĐẾM SỐ & LÀM TOÁN TRÊN KHUNG VUÔNG 32 LED] <<<\r\n");
    printf(">>> [Phím A..Z: Bản đồ chữ cái cầu vồng quanh 4 cạnh | Phím 0..10: Đoàn tàu đếm số cầu vồng] <<<\r\n");
    printf(">>> [4 Phím Mũi Tên: Học 4 hướng (Lên/Phải/Xuống/Trái) sáng đúng 4 cạnh hình vuông!] <<<\r\n");
    printf(">>> [Phím 'M' hoặc '+', '-', '=': KÍCH HOẠT TRÒ CHƠI BÉ TẬP LÀM TOÁN THẦN TỐC (MATH QUIZ)!] <<<\r\n");
    printf(">>> [BTN 6 (Action): Đố vui tìm chữ cái hoặc chuyển câu hỏi toán] <<<\r\n\r\n");
}

static bool mode_phonics_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // --- XỬ LÝ TRONG TRÒ CHƠI "BÉ TẬP LÀM TOÁN THẦN TỐC" (MATH GENIUS QUIZ) ---
    if (s_math_active) {
        // Phím ESC: Thoát khỏi trò chơi làm toán, quay về chế độ tự do
        if (kc == 0x29) {
            s_math_active = false;
            s_math_state = MATH_STATE_IDLE;
            printf("\r\n>>> [BÉ QUAY LẠI CHẾ ĐỘ TỰ DO: HỌC CHỮ CÁI & ĐẾM SỐ] <<<\r\n\r\n");
            led_effects_color_burst(0, 200, 255);
            return true;
        }

        // Phím Space hoặc '?' : Phát lại câu hỏi toán cho bé nghe
        if (c == ' ' || kc == 0x2C || c == '?') {
            s_math_state = MATH_STATE_SPEAK_A;
            s_math_timer_ms = 0;
            printf("\r\n>>> [PHÁT LẠI CÂU ĐỐ]: %d %c %d = ? <<<\r\n",
                   s_math_a, (s_math_op == 0 ? '+' : '-'), s_math_b);
            return true;
        }

        // Phím '+' hoặc '-' hoặc '=': Bỏ qua / chuyển sang câu đố mới
        if (c == '+' || c == '-' || c == '=' || upper == 'N') {
            math_quiz_start_question();
            return true;
        }

        // Nhận diện phím số bé bấm (0..10)
        int8_t ans = -1;
        if (c >= '0' && c <= '9') ans = (int8_t)(c - '0');
        else if (kc >= 0x59 && kc <= 0x61) ans = (int8_t)(kc - 0x59 + 1); // Numpad 1..9
        else if (kc == 0x62) ans = 0; // Numpad 0
        else if (kc == 0x43 || (kc == 0x27 && c == ')')) ans = 10; // F10 hoặc Shift+0 = 10

        if (ans >= 0) {
            if (s_math_state == MATH_STATE_WAIT_INPUT) {
                s_math_user_ans = ans;
                if (ans == s_math_target) {
                    s_math_score += 10;
                    s_math_streak++;
                    printf("\r\n============================================================\r\n");
                    printf(">>> [CHÍNH XÁC! HOAN HÔ BÉ!]: %d %c %d = %d <<<\r\n",
                           s_math_a, (s_math_op == 0 ? '+' : '-'), s_math_b, ans);
                    printf(">>> [ĐIỂM SỐ: %d ĐIỂM | CHUỖI ĐÚNG LIÊN TIẾP (STREAK): %d] <<<\r\n",
                           s_math_score, s_math_streak);
                    printf("============================================================\r\n\r\n");
                    sb_play_praise();
                    led_effects_celebration();
                    s_math_state = MATH_STATE_CELEBRATE;
                    s_math_timer_ms = 2400;
                } else {
                    s_math_streak = 0;
                    sb_play_typewriter(TYPEWRITER_BACKSPACE);
                    printf(" -> [CHƯA ĐÚNG RỒI]: Bé bấm số %d. %d %c %d không phải bằng %d đâu!\r\n",
                           ans, s_math_a, (s_math_op == 0 ? '+' : '-'), s_math_b, ans);
                    printf(" -> [GỢI Ý]: Bé hãy đếm số bóng đèn bên Trái (Cyan) và bên Phải (Cam) nhé!\r\n");
                    led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, ans, false);
                }
            }
            return true;
        }

        return true;
    }

    // --- KHI CHƯA VÀO MATH QUIZ: Phím '+', '-', '=', 'M' kích hoạt TRÒ CHƠI LÀM TOÁN ---
    if (c == '+' || c == '-' || c == '=' || upper == 'M') {
        math_quiz_start_question();
        return true;
    }

    // 1. Phím chữ cái A - Z: Phát âm ngữ âm tiếng Việt & thắp sáng vị trí chữ cái trên khung vuông
    if (upper >= 'A' && upper <= 'Z') {
        // Kiểm tra câu trả lời nếu đang trong trò chơi Đố Vui Tìm Chữ
        if (s_quiz_active) {
            if (upper == s_quiz_target) {
                s_quiz_active = false;
                audio_play_praise();
                led_effects_celebration();
                printf("\r\n>>> [ĐỐ VUI: CHÍNH XÁC! Bé đã tìm đúng chữ cái '%c'! Hoan hô bé!] <<<\r\n\r\n", upper);
                s_prev_letter = 0;
                return true;
            } else {
                printf(" -> [ĐỐ VUI: Chưa đúng chữ '%c' rồi, bé tìm lại xem chữ '%c' ở đâu nhé!]\r\n", s_quiz_target, s_quiz_target);
            }
        }

        audio_play_phonics_letter(upper);
        led_effects_phonics_letter(upper);

        const char *spell_text = s_spell_dict[upper - 'A'];

        // Ghép vần 2 chữ cái cơ bản: Phụ âm (B, C, D, G, M, N, T, V) + Nguyên âm (A, E, O, U, I)
        if (s_prev_letter != 0 && (upper == 'A' || upper == 'E' || upper == 'O' || upper == 'U' || upper == 'I')) {
            printf(" -> [BÉ HỌC ĐÁNH VẦN: %s]\r\n", spell_text);
            printf(" >>> [GHÉP VẦN THẦN KỲ]: Chữ '%c' ghép với chữ '%c' tạo thành tiếng: [ %c%c ]!\r\n",
                   s_prev_letter, upper, s_prev_letter, upper);
            audio_play_action_feedback();
            s_prev_letter = 0;
        } else {
            printf(" -> [BÉ HỌC ĐÁNH VẦN: %s]\r\n", spell_text);
            s_prev_letter = upper;
        }
        return true;
    }

    // 2. Phím F10 (0x43) hoặc Shift+0 (c == ')'): Bé học đếm SỐ 10 hoàn chỉnh!
    if (kc == 0x43 || (kc == 0x27 && c == ')')) {
        printf(" -> [BÉ HỌC SỐ: SỐ 10 - ĐẾM ĐỦ 10 NHỊP TRỐNG & 10 BÓNG ĐÈN CẦU VỒNG!]\r\n");
        audio_play_counting_number(10);
        return true;
    }

    // 3. Phím số 0..9 trên hàng phím chính hoặc Numpad (Khi không giữ Shift)
    if ((c >= '0' && c <= '9') || (kc >= 0x59 && kc <= 0x62)) {
        uint8_t num = 0;
        if (c >= '0' && c <= '9') num = (uint8_t)(c - '0');
        else if (kc >= 0x59 && kc <= 0x61) num = (uint8_t)(kc - 0x59 + 1); // Numpad 1..9
        else if (kc == 0x62) num = 0; // Numpad 0 = 0

        printf(" -> [BÉ HỌC SỐ: SỐ %d]\r\n", num);
        audio_play_counting_number(num);
        return true;
    }

    // 4. 4 Phím Mũi Tên: Học phương hướng trực quan trên 4 Cạnh Hình Vuông (Trên / Phải / Dưới / Trái)
    if (kc >= 0x4F && kc <= 0x52) {
        // Đảo 0x52 (Lên) <-> 0x51 (Xuống) cho đúng hướng quét thực tế của người dùng:
        // - Mũi tên Lên (0x52): Quét từ Cạnh Dưới (side 2) bốc LÊN Cạnh Trên
        // - Mũi tên Xuống (0x51): Quét từ Cạnh Trên (side 0) dội XUỐNG Cạnh Dưới
        // - Mũi tên Phải (0x4F): Quét từ Cạnh Phải (side 1) sang Cạnh Trái
        // - Mũi tên Trái (0x50): Quét từ Cạnh Trái (side 3) sang Cạnh Phải
        uint8_t side = (kc == 0x52) ? 2 : (kc == 0x4F) ? 1 : (kc == 0x51) ? 0 : 3;
        uint8_t r = 0, g = 220, b = 255;
        if (kc == 0x52)      { r = 50;  g = 255; b = 80;  } // Lên: Xanh lá tươi
        else if (kc == 0x51) { r = 255; g = 100; b = 0;   } // Xuống: Cam rực rỡ
        else if (kc == 0x4F) { r = 0;   g = 180; b = 255; } // Phải: Xanh cyan biển
        else if (kc == 0x50) { r = 255; g = 50;  b = 200; } // Trái: Hồng sen cánh chuồn

        if (!sb_play_special_key(kc)) {
            audio_play_special_key(kc);
        }
        led_effects_direction_sweep(side, r, g, b);
        printf(" -> [BÉ HỌC HƯỚNG: QUÉT TỪ TÂM CẠNH %u SANG TÂM CẠNH ĐỐI DIỆN (0x%02X)]\r\n", (unsigned int)side, kc);
        return true;
    }

    // 5. Phím ký tự & dấu câu (+, -, *, /, =, ?, !, @, #...)
    if (c != 0 && c != '\r' && c != '\n' && c != '\b' && c != '\t' && c != ' ' && sb_play_symbol(c)) {
        led_effects_piano_key((c >> 3) & 3, c & 7, 2, 80, 220, 255);
        printf(" -> [BÉ HỌC DẤU CÂU & KÝ TỰ: '%c']\r\n", c);
        return true;
    }

    // 6. Phím chức năng đặc biệt (ESC, F1..F9, Space, Enter, Backspace, Tab, Shift, Ctrl, Alt...)
    if (sb_play_special_key(kc)) {
        led_effects_piano_key((kc >> 3) & 3, kc & 7, 0, 255, 215, 0);
        printf(" -> [BÉ HỌC PHÍM CHỨC NĂNG: 0x%02X]\r\n", kc);
        return true;
    }

    // 7. Khám phá các phím khác
    audio_play_special_key(kc);
    led_effects_spark();
    printf(" -> [BÉ KHÁM PHÁ BÀN PHÍM: 0x%02X]\r\n", kc);
    return true;
}

static void mode_phonics_background_tick(uint32_t delta_ms) {
    if (!s_math_active) return;

    // Timer nhấp nháy đèn dẫn đường
    s_math_pulse_timer += delta_ms;
    if (s_math_pulse_timer >= 280) {
        s_math_pulse_timer = 0;
        s_math_pulse_toggle = !s_math_pulse_toggle;
    }

    if (s_math_timer_ms > delta_ms) {
        s_math_timer_ms -= delta_ms;
    } else {
        s_math_timer_ms = 0;
    }

    switch (s_math_state) {
        case MATH_STATE_SPEAK_A:
            if (s_math_timer_ms == 0) {
                uint32_t dur = 550;
                sb_play_number(s_math_a, &dur);
                s_math_state = MATH_STATE_SPEAK_OP;
                s_math_timer_ms = dur + 120;
            }
            break;

        case MATH_STATE_SPEAK_OP:
            if (s_math_timer_ms == 0) {
                sb_play_symbol(s_math_op == 0 ? '+' : '-');
                s_math_state = MATH_STATE_SPEAK_B;
                s_math_timer_ms = 720;
            }
            break;

        case MATH_STATE_SPEAK_B:
            if (s_math_timer_ms == 0) {
                uint32_t dur = 550;
                sb_play_number(s_math_b, &dur);
                s_math_state = MATH_STATE_SPEAK_EQUAL;
                s_math_timer_ms = dur + 120;
            }
            break;

        case MATH_STATE_SPEAK_EQUAL:
            if (s_math_timer_ms == 0) {
                sb_play_symbol('=');
                s_math_state = MATH_STATE_WAIT_INPUT;
                s_math_timer_ms = 700;
            }
            break;

        case MATH_STATE_WAIT_INPUT:
            led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, s_math_user_ans, s_math_pulse_toggle);
            break;

        case MATH_STATE_CELEBRATE:
            if (s_math_timer_ms == 0) {
                math_quiz_start_question();
            }
            break;

        default:
            break;
    }
}

static void mode_phonics_handle_action(void) {
    if (s_math_active) {
        // Đang làm toán: Đổi sang câu hỏi toán tiếp theo
        math_quiz_start_question();
        return;
    }

    s_quiz_active = true;
    s_quiz_target = 'A' + (rand() % 26);
    audio_play_phonics_letter(s_quiz_target);
    printf("\r\n======================================================\r\n");
    printf(">>> [MINI-GAME ĐỐ VUI]: Đố bé tìm thấy chữ cái [ %c ] ở đâu nào? <<<\r\n", s_quiz_target);
    printf(">>> Hãy nhìn bản đồ chữ cái trên bàn phím và bấm đúng chữ nhé! <<<\r\n");
    printf(">>> (Mẹo: Nhấn phím 'M' hoặc '+', '-' để bật Đố Làm Toán Thần Tốc) <<<\r\n");
    printf("======================================================\r\n\r\n");
    fflush(stdout);
    led_effects_square_spin(true, 0, 200, 255);
}

const toy_mode_driver_t g_mode_phonics_driver = {
    .id                 = 8,
    .name               = "9. BÉ HỌC CHỮ CÁI, TẬP ĐẾM & LÀM TOÁN (PHONICS & MATH)",
    .short_desc         = "Học chữ cái, đếm số 0-10 & Trò chơi làm toán cộng trừ thần tốc trên khung vuông 32 LED",
    .init               = NULL,
    .on_enter           = mode_phonics_on_enter,
    .on_exit            = NULL,
    .handle_key         = mode_phonics_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_phonics_background_tick,
    .handle_action      = mode_phonics_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
