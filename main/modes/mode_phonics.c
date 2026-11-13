#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"
#include "math_audio_data.h"
#include "mode_demo.h"

static bool s_quiz_active = false;
static char s_quiz_target = 'A';
static char s_prev_letter = 0;

// 2 Phân hệ riêng biệt trong Chế độ 9 (Chuyển đổi qua Caps Lock & Num Lock)
typedef enum {
    SUBMODE_PHONICS = 0, // Chế độ 1: Gõ phím đọc tên phím & học chữ cái, số, ký tự
    SUBMODE_MATH    = 1  // Chế độ 2: Trò chơi bé tập làm toán thần tốc
} phonics_submode_t;

static phonics_submode_t s_submode = SUBMODE_PHONICS;

// Trạng thái Trò chơi "Bé Tập Làm Toán Thần Tốc" (Math Genius Quiz)
typedef enum {
    MATH_STATE_IDLE = 0,
    MATH_STATE_SPEAK_A,
    MATH_STATE_SPEAK_OP,
    MATH_STATE_SPEAK_B,
    MATH_STATE_SPEAK_EQUAL,
    MATH_STATE_WAIT_INPUT,
    MATH_STATE_ANS_BANG,
    MATH_STATE_ANS_VAL,
    MATH_STATE_ANS_PRAISE,
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

static const char *s_math_num_words[11] = {
    "Không", "Một", "Hai", "Ba", "Bốn", "Năm", "Sáu", "Bảy", "Tám", "Chín", "Mười"
};

static void math_quiz_start_question_delayed(uint32_t delay_ms) {
    s_math_active = true;
    s_quiz_active = false; // Tắt quiz chữ nếu đang mở

    s_math_op = (rand() % 2); // 0 = +, 1 = -
    if (s_math_op == 0) {
        // Phép cộng (+): A + B <= 9 (đáp án 0..9, khớp 8 LED mỗi bên và hàng phím số)
        s_math_a = 1 + (rand() % 5);               // 1..5
        s_math_b = 1 + (rand() % (9 - s_math_a));  // 1..(9 - A)
        s_math_target = (int8_t)(s_math_a + s_math_b);
    } else {
        // Phép trừ (-): A - B >= 0, A <= 8
        s_math_a = 2 + (rand() % 7);               // 2..8
        s_math_b = 1 + (rand() % s_math_a);        // 1..A
        s_math_target = (int8_t)(s_math_a - s_math_b);
    }

    s_math_user_ans = -1;
    s_math_state = MATH_STATE_SPEAK_A;
    s_math_timer_ms = delay_ms;

    printf("\r\n============================================================\r\n");
    printf(">>> [TRÒ CHƠI: BÉ TẬP LÀM TOÁN THẦN TỐC - CÂU ĐỐ SỐ %d] <<<\r\n", (s_math_score / 10) + 1);
    printf("           >>> [   %d  %c  %d  =  ?   ] <<<\r\n",
           s_math_a, (s_math_op == 0 ? '+' : '-'), s_math_b);
    printf("------------------------------------------------------------\r\n");
    printf(" - Giọng đọc        : \"%s %s %s bằng mấy?\"\r\n",
           s_math_num_words[s_math_a <= 10 ? s_math_a : 0],
           (s_math_op == 0 ? "cộng" : "trừ"),
           s_math_num_words[s_math_b <= 10 ? s_math_b : 0]);
    printf(" - Cạnh Trái (Cyan) : %d bóng đèn số thứ nhất\r\n", s_math_a);
    printf(" - Cạnh Phải (Cam)  : %d bóng đèn số thứ hai\r\n", s_math_b);
    printf(" - Cạnh Trên        : Dấu [%c] (%s)\r\n",
           (s_math_op == 0 ? '+' : '-'), (s_math_op == 0 ? "Vàng - Phép Cộng" : "Đỏ - Phép Trừ"));
    printf(" - Cạnh Dưới        : Chờ bé bấm phím số đáp án (0..9)!\r\n");
    printf(" - Phím SPACE / '?' : Nghe lại câu hỏi | Phím ESC: Thoát trò chơi\r\n");
    printf("============================================================\r\n\r\n");
    fflush(stdout);

    led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, -1, true);
}

static void math_quiz_start_question(void) {
    math_quiz_start_question_delayed(80);
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
    s_submode = SUBMODE_PHONICS; // Mặc định vào Chế độ Gõ phím đọc tên phím
    s_math_active = false;
    s_math_state = MATH_STATE_IDLE;
    s_math_timer_ms = 0;
    s_math_score = 0;
    s_math_streak = 0;
    s_quiz_active = false;
    s_prev_letter = 0;

    mode_demo_stop(); // Chắc chắn dừng mọi bản demo khác để tránh chồng âm thanh

    printf("\r\n============================================================\r\n");
    printf(">>> [CHẾ ĐỘ 9: BÉ HỌC CHỮ CÁI, TẬP ĐẾM & LÀM TOÁN (PHONICS & MATH)] <<<\r\n");
    printf(">>> [CHẾ ĐỘ HIỆN TẠI]: GÕ PHÍM ĐỌC TÊN PHÍM & HỌC CHỮ CÁI <<<\r\n");
    printf("------------------------------------------------------------\r\n");
    printf(" - Bé gõ phím bất kỳ (A..Z, 0..10, Enter, Mũi tên...): ĐỌC TO TÊN PHÍM!\r\n");
    printf(" - Bấm phím [NUM LOCK] (hoặc 'M' / Action): Chuyển sang TRÒ CHƠI BÉ TẬP LÀM TOÁN!\r\n");
    printf(" - Bấm phím [CAPS LOCK]: Chuyển lại CHẾ ĐỘ GÕ PHÍM ĐỌC TÊN PHÍM!\r\n");
    printf(" (Hai chế độ hoạt động riêng biệt 100%%, một chế độ chỉ hoạt động một lúc)\r\n");
    printf("============================================================\r\n\r\n");
    fflush(stdout);
}

static void mode_phonics_on_exit(void) {
    s_submode = SUBMODE_PHONICS;
    s_math_active = false;
    s_math_state = MATH_STATE_IDLE;
    s_math_timer_ms = 0;
    s_quiz_active = false;
    s_prev_letter = 0;
}

static bool mode_phonics_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // =========================================================================
    // 1. PHÍM CHUYỂN ĐỔI CHẾ ĐỘ: CAPS LOCK <-> NUM LOCK
    // =========================================================================

    // Phím CAPS LOCK (0x39): Chuyển sang CHẾ ĐỘ GÕ PHÍM ĐỌC TÊN PHÍM
    if (kc == 0x39) {
        if (s_submode != SUBMODE_PHONICS) {
            s_submode = SUBMODE_PHONICS;
            s_math_active = false;
            s_math_state = MATH_STATE_IDLE;
            s_math_timer_ms = 0;
            printf("\r\n============================================================\r\n");
            printf(">>> [CHUYỂN SANG: CHẾ ĐỘ GÕ PHÍM ĐỌC TÊN PHÍM & HỌC CHỮ CÁI] <<<\r\n");
            printf(">>> - Gõ A..Z : Đọc tên chữ cái & ghép vần thần kỳ!\r\n");
            printf(">>> - Gõ 0..10: Đọc đếm số & thắp sáng đoàn tàu cầu vồng!\r\n");
            printf(">>> - Gõ Phím chức năng / Mũi tên: Đọc to tên phím!\r\n");
            printf(">>> (Nhấn NUM LOCK để chuyển sang Trò Chơi Bé Tập Làm Toán)\r\n");
            printf("============================================================\r\n\r\n");
            fflush(stdout);
            led_effects_color_burst(0, 200, 255);
            sb_play_special_key(0x39); // Đọc to: "Phím Caps Lock"
        } else {
            sb_play_special_key(0x39);
        }
        return true;
    }

    // Phím NUM LOCK (0x53) hoặc 'M': Chuyển sang TRÒ CHƠI BÉ TẬP LÀM TOÁN
    if (kc == 0x53 || upper == 'M') {
        if (s_submode != SUBMODE_MATH) {
            s_submode = SUBMODE_MATH;
            printf("\r\n============================================================\r\n");
            printf(">>> [CHUYỂN SANG: TRÒ CHƠI BÉ TẬP LÀM TOÁN THẦN TỐC (MATH QUIZ)] <<<\r\n");
            printf(">>> - Loa đọc câu đố: \"A cộng/trừ B bằng mấy?\"\r\n");
            printf(">>> - Đèn LED 4 cạnh: Trái (A) - Phải (B) - Trên (+/-) - Dưới (Đáp án)\r\n");
            printf(">>> - Phím số 0..9: Bấm đáp án (Đọc to: \"Bằng X! Chính xác, bé giỏi quá!\")\r\n");
            printf(">>> (Nhấn CAPS LOCK để chuyển sang Chế Độ Gõ Phím Đọc Tên Phím)\r\n");
            printf("============================================================\r\n\r\n");
            fflush(stdout);
            led_effects_color_burst(255, 215, 0);
            sb_play_special_key(0x53); // Đọc to: "Phím Num Lock"
            math_quiz_start_question_delayed(700); // Chờ đọc xong "Phím Num Lock" rồi đọc câu đố!
        } else {
            // Đang ở trong chế độ làm toán: bấm Num Lock chuyển sang câu đố mới!
            math_quiz_start_question();
        }
        return true;
    }

    // Phím ESC (0x29):
    if (kc == 0x29) {
        if (s_submode == SUBMODE_MATH) {
            s_submode = SUBMODE_PHONICS;
            s_math_active = false;
            s_math_state = MATH_STATE_IDLE;
            s_math_timer_ms = 0;
            printf("\r\n>>> [BÉ QUAY LẠI CHẾ ĐỘ TỰ DO: GÕ PHÍM ĐỌC TÊN PHÍM] <<<\r\n\r\n");
            fflush(stdout);
            led_effects_color_burst(0, 200, 255);
            sb_play_special_key(0x29); // Đọc to: "Phím Escape"
            return true;
        }
        sb_play_special_key(0x29);
        return true;
    }

    // =========================================================================
    // 2. KHI ĐANG Ở TRÒ CHƠI BÉ TẬP LÀM TOÁN (SUBMODE_MATH)
    // =========================================================================
    if (s_submode == SUBMODE_MATH) {
        // Phím Space hoặc '?' : Phát lại câu hỏi toán cho bé nghe
        if (c == ' ' || kc == 0x2C || c == '?') {
            s_math_state = MATH_STATE_SPEAK_A;
            s_math_timer_ms = 0;
            printf("\r\n>>> [PHÁT LẠI CÂU ĐỐ]: %d %c %d = ? <<<\r\n",
                   s_math_a, (s_math_op == 0 ? '+' : '-'), s_math_b);
            return true;
        }

        // Phím '+' hoặc '-' hoặc '=' hoặc 'N': Chuyển sang câu đố mới
        if (c == '+' || c == '-' || c == '=' || upper == 'N') {
            math_quiz_start_question();
            return true;
        }

        // Nhận diện phím số bé bấm (0..9) làm đáp án
        int8_t ans = -1;
        if (c >= '0' && c <= '9') ans = (int8_t)(c - '0');
        else if (kc >= 0x59 && kc <= 0x61) ans = (int8_t)(kc - 0x59 + 1); // Numpad 1..9
        else if (kc == 0x62) ans = 0; // Numpad 0

        if (ans >= 0) {
            // Cho phép bé trả lời ngay cả khi câu hỏi đang đọc hoặc khi đang chờ bấm!
            // Chỉ bỏ qua khi đang đọc chuỗi khen đáp án (ANS_BANG, ANS_VAL, ANS_PRAISE)
            if (s_math_state != MATH_STATE_ANS_BANG &&
                s_math_state != MATH_STATE_ANS_VAL &&
                s_math_state != MATH_STATE_ANS_PRAISE &&
                s_math_state != MATH_STATE_CELEBRATE) {
                s_math_user_ans = ans;
                if (ans == s_math_target) {
                    s_math_score += 10;
                    s_math_streak++;
                    printf("\r\n============================================================\r\n");
                    printf(">>> [CHÍNH XÁC! BÉ GIỎI QUÁ!]: %d %c %d = %d <<<\r\n",
                           s_math_a, (s_math_op == 0 ? '+' : '-'), s_math_b, ans);
                    printf(">>> [ĐIỂM SỐ: %d ĐIỂM | CHUỖI ĐÚNG LIÊN TIẾP (STREAK): %d] <<<\r\n",
                           s_math_score, s_math_streak);
                    printf("============================================================\r\n\r\n");
                    fflush(stdout);

                    led_effects_celebration();
                    led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, ans, false);

                    // Bắt đầu chuỗi đọc kết quả: "Bằng" -> "<Đáp án>" -> "Chính xác, bé giỏi quá!"
                    s_math_state = MATH_STATE_ANS_BANG;
                    s_math_timer_ms = 0;
                } else {
                    s_math_streak = 0;
                    printf(" -> [CHƯA ĐÚNG RỒI]: Bé bấm số %d. %d %c %d không phải bằng %d đâu!\r\n",
                           ans, s_math_a, (s_math_op == 0 ? '+' : '-'), s_math_b, ans);
                    printf(" -> [GỢI Ý]: Bé hãy đếm số bóng đèn bên Trái (Cyan) và bên Phải (Cam) nhé!\r\n");
                    fflush(stdout);

                    uint32_t dur = 600;
                    math_audio_play(MATH_SND_HINT_WRONG, &dur);
                    led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, ans, false);
                    s_math_state = MATH_STATE_WAIT_INPUT;
                    s_math_timer_ms = dur + 100;
                }
            }
            return true;
        }

        // Khi đang ở Trò chơi làm toán, các phím khác không được chạy đè âm thanh
        return true;
    }

    // =========================================================================
    // 3. KHI ĐANG Ở CHẾ ĐỘ GÕ PHÍM ĐỌC TÊN PHÍM (SUBMODE_PHONICS)
    // =========================================================================

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
    if (s_submode != SUBMODE_MATH || !s_math_active) return;

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

    uint32_t dur = 300;
    switch (s_math_state) {
        case MATH_STATE_SPEAK_A:
            if (s_math_timer_ms == 0) {
                math_audio_play((math_snd_id_t)(MATH_SND_NUM_0 + s_math_a), &dur);
                s_math_state = MATH_STATE_SPEAK_OP;
                s_math_timer_ms = dur + 60;
            }
            break;

        case MATH_STATE_SPEAK_OP:
            if (s_math_timer_ms == 0) {
                math_audio_play(s_math_op == 0 ? MATH_SND_PLUS : MATH_SND_MINUS, &dur);
                s_math_state = MATH_STATE_SPEAK_B;
                s_math_timer_ms = dur + 60;
            }
            break;

        case MATH_STATE_SPEAK_B:
            if (s_math_timer_ms == 0) {
                math_audio_play((math_snd_id_t)(MATH_SND_NUM_0 + s_math_b), &dur);
                s_math_state = MATH_STATE_SPEAK_EQUAL;
                s_math_timer_ms = dur + 60;
            }
            break;

        case MATH_STATE_SPEAK_EQUAL:
            if (s_math_timer_ms == 0) {
                math_audio_play(MATH_SND_ASK_EQUALS, &dur);
                s_math_state = MATH_STATE_WAIT_INPUT;
                s_math_timer_ms = dur + 80;
            }
            break;

        case MATH_STATE_WAIT_INPUT:
            led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, s_math_user_ans, s_math_pulse_toggle);
            break;

        case MATH_STATE_ANS_BANG:
            if (s_math_timer_ms == 0) {
                math_audio_play(MATH_SND_ANS_BANG, &dur); // Phát âm: "Bằng"
                s_math_state = MATH_STATE_ANS_VAL;
                s_math_timer_ms = dur + 40;
            }
            break;

        case MATH_STATE_ANS_VAL:
            if (s_math_timer_ms == 0) {
                math_audio_play((math_snd_id_t)(MATH_SND_NUM_0 + s_math_target), &dur); // e.g. "Một", "Hai"...
                s_math_state = MATH_STATE_ANS_PRAISE;
                s_math_timer_ms = dur + 50;
            }
            break;

        case MATH_STATE_ANS_PRAISE:
            if (s_math_timer_ms == 0) {
                math_audio_play(MATH_SND_PRAISE_CORRECT, &dur); // Phát âm: "Chính xác, bé giỏi quá!"
                s_math_state = MATH_STATE_CELEBRATE;
                s_math_timer_ms = dur + 400;
            }
            break;

        case MATH_STATE_CELEBRATE:
            if (s_math_timer_ms == 0) {
                math_quiz_start_question_delayed(400);
            }
            break;

        default:
            break;
    }
}

static void mode_phonics_handle_action(void) {
    if (s_submode == SUBMODE_MATH) {
        // Đang ở làm toán: bấm Action đổi câu đố mới
        math_quiz_start_question();
    } else {
        // Đang ở gõ phím đọc tên phím: bấm Action chuyển sang Trò chơi làm toán (tương tự Num Lock)
        s_submode = SUBMODE_MATH;
        led_effects_color_burst(255, 215, 0);
        math_quiz_start_question_delayed(80);
    }
}

const toy_mode_driver_t g_mode_phonics_driver = {
    .id                 = 8,
    .name               = "9. BÉ HỌC CHỮ CÁI, TẬP ĐẾM & LÀM TOÁN (PHONICS & MATH)",
    .short_desc         = "Học chữ cái, đếm số 0-10 & Trò chơi làm toán cộng trừ thần tốc trên khung vuông 32 LED",
    .init               = NULL,
    .on_enter           = mode_phonics_on_enter,
    .on_exit            = mode_phonics_on_exit,
    .handle_key         = mode_phonics_handle_key,
    .handle_key_release = NULL,
    .background_tick    = mode_phonics_background_tick,
    .handle_action      = mode_phonics_handle_action,
    .handle_encoder     = NULL,
    .draw_screen        = NULL
};
