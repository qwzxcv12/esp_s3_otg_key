#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "mode_interface.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"
#include "math_audio_data.h"
#include "mode_demo.h"

// 3 Chế độ hoạt động độc lập trong Chế độ 9
typedef enum {
    SUBMODE_DEFAULT  = 0, // Chế độ Mặc định: Gõ phím nào đọc to ký tự/tên phím đó tự do
    SUBMODE_SPELLING = 1, // Chế độ Caps Lock: Trò chơi Bé tập đánh vần & tìm chữ cái
    SUBMODE_MATH     = 2  // Chế độ Num Lock: Trò chơi Bé tập làm toán thần tốc
} phonics_submode_t;

static phonics_submode_t s_submode = SUBMODE_DEFAULT;

// Trạng thái Trò chơi "Bé Tập Đánh Vần & Tìm Chữ Cái" (Spelling Quiz)
typedef enum {
    SPELL_STATE_IDLE = 0,
    SPELL_STATE_SPEAK_INTRO,   // "Đố bé tìm thấy"
    SPELL_STATE_SPEAK_LETTER,  // "Chữ [X]" (từ soundbank qua sb_play_letter_dur)
    SPELL_STATE_SPEAK_OUTRO,   // "ở đâu nào?"
    SPELL_STATE_WAIT_INPUT,    // Chờ bé bấm phím chữ cái
    SPELL_STATE_CORRECT_PRAISE,// "Chính xác! Bé giỏi quá!"
    SPELL_STATE_HINT_WRONG     // "Chưa đúng rồi, bé tìm lại nhé!" -> tự động đọc lại câu hỏi
} spelling_quiz_state_t;

static spelling_quiz_state_t s_spell_state = SPELL_STATE_IDLE;
static char s_spell_target = 'A';
static char s_spell_user_guess = 0;
static uint32_t s_spell_timer_ms = 0;
static uint16_t s_spell_score = 0;
static uint8_t s_spell_streak = 0;

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
    MATH_STATE_CELEBRATE,
    MATH_STATE_HINT_WRONG
} math_quiz_state_t;

static bool s_math_active = false;
static math_quiz_state_t s_math_state = MATH_STATE_IDLE;
static uint8_t s_math_a = 0;
static uint8_t s_math_b = 0;
static uint8_t s_math_op = 0; // 0 = Cộng (+), 1 = Trừ (-)
static int8_t s_math_target = 0;
static int8_t s_math_user_ans = -1;
static uint32_t s_math_timer_ms = 0;
static uint16_t s_math_score = 0;
static uint8_t s_math_streak = 0;

static uint32_t s_pulse_timer = 0;
static bool s_pulse_toggle = false;
static char s_prev_letter = 0;

static const char *s_math_num_words[11] = {
    "Không", "Một", "Hai", "Ba", "Bốn", "Năm", "Sáu", "Bảy", "Tám", "Chín", "Mười"
};

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

// =========================================================================
// HÀM KHỞI TẠO CÂU ĐỐ TOÁN HỌC (MATH QUIZ)
// =========================================================================
static void math_quiz_start_question_delayed(uint32_t delay_ms) {
    s_submode = SUBMODE_MATH;
    s_math_active = true;
    s_spell_state = SPELL_STATE_IDLE; // Tắt quiz đánh vần

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
    printf(" - Phím SPACE / '?' : Nghe lại câu hỏi | Phím ESC / NUM LOCK: Thoát trò chơi\r\n");
    printf("============================================================\r\n\r\n");
    fflush(stdout);

    led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, -1, true);
}

static void math_quiz_start_question(void) {
    math_quiz_start_question_delayed(80);
}

// =========================================================================
// HÀM KHỞI TẠO CÂU ĐỐ ĐÁNH VẦN & TÌM CHỮ (SPELLING QUIZ)
// =========================================================================
static void spelling_quiz_start_question_delayed(uint32_t delay_ms) {
    s_submode = SUBMODE_SPELLING;
    s_math_active = false;
    s_math_state = MATH_STATE_IDLE;

    // Chọn ngẫu nhiên chữ cái khác với chữ cái trước đó
    char next_letter;
    do {
        next_letter = 'A' + (rand() % 26);
    } while (next_letter == s_spell_target && (rand() % 4 != 0));
    s_spell_target = next_letter;

    s_spell_user_guess = 0;
    s_spell_state = SPELL_STATE_SPEAK_INTRO;
    s_spell_timer_ms = delay_ms;

    printf("\r\n============================================================\r\n");
    printf(">>> [TRÒ CHƠI: BÉ TẬP ĐÁNH VẦN & TÌM CHỮ CÁI - CÂU ĐỐ SỐ %d] <<<\r\n", (s_spell_score / 10) + 1);
    printf("           >>> [   ĐỐ BÉ TÌM THẤY CHỮ '%c'   ] <<<\r\n", s_spell_target);
    printf("------------------------------------------------------------\r\n");
    printf(" - Giọng đọc        : \"Đố bé tìm thấy Chữ %c ở đâu nào?\"\r\n", s_spell_target);
    printf(" - Từ gợi ý         : %s\r\n", s_spell_dict[s_spell_target - 'A']);
    printf(" - Đèn LED          : Nhấp nháy dẫn đường tại vị trí chữ '%c' trên khung 32 LED!\r\n", s_spell_target);
    printf(" - Phím SPACE / '?' : Nghe lại câu hỏi | Phím ESC / CAPS LOCK: Thoát trò chơi\r\n");
    printf("============================================================\r\n\r\n");
    fflush(stdout);

    led_effects_spelling_quiz_display(s_spell_target, 0, true);
}

static void spelling_quiz_start_question(void) {
    spelling_quiz_start_question_delayed(80);
}

// =========================================================================
// VÀO / THOÁT CHẾ ĐỘ 9
// =========================================================================
static void mode_phonics_on_enter(void) {
    s_submode = SUBMODE_DEFAULT; // Mặc định vào Chế độ Gõ phím đọc tên phím
    s_math_active = false;
    s_math_state = MATH_STATE_IDLE;
    s_math_timer_ms = 0;
    s_math_score = 0;
    s_math_streak = 0;
    s_spell_state = SPELL_STATE_IDLE;
    s_spell_timer_ms = 0;
    s_spell_user_guess = 0;
    s_spell_score = 0;
    s_spell_streak = 0;
    s_prev_letter = 0;

    mode_demo_stop(); // Chắc chắn dừng mọi bản demo khác để tránh chồng âm thanh

    printf("\r\n============================================================\r\n");
    printf(">>> [CHẾ ĐỘ 9: BÉ HỌC CHỮ CÁI, TẬP ĐẾM & LÀM TOÁN (PHONICS & MATH)] <<<\r\n");
    printf(">>> [CHẾ ĐỘ HIỆN TẠI]: GÕ PHÍM ĐỌC TÊN PHÍM (MẶC ĐỊNH) <<<\r\n");
    printf("------------------------------------------------------------\r\n");
    printf(" - Bé gõ phím bất kỳ (A..Z, 0..10, Enter, Mũi tên...): ĐỌC TO TÊN PHÍM!\r\n");
    printf(" - Bấm phím [CAPS LOCK]: Vào TRÒ CHƠI BÉ TẬP ĐÁNH VẦN & TÌM CHỮ CÁI!\r\n");
    printf(" - Bấm phím [NUM LOCK]: Vào TRÒ CHƠI BÉ TẬP LÀM TOÁN THẦN TỐC!\r\n");
    printf(" - Bấm phím [ESC]: Trở về chế độ gõ tự do mặc định bất cứ lúc nào\r\n");
    printf(" (3 chế độ hoạt động riêng biệt 100%%, một chế độ chỉ hoạt động một lúc)\r\n");
    printf("============================================================\r\n\r\n");
    fflush(stdout);
}

static void mode_phonics_on_exit(void) {
    s_submode = SUBMODE_DEFAULT;
    s_math_active = false;
    s_math_state = MATH_STATE_IDLE;
    s_math_timer_ms = 0;
    s_spell_state = SPELL_STATE_IDLE;
    s_spell_timer_ms = 0;
    s_prev_letter = 0;
}

// =========================================================================
// XỬ LÝ PHÍM BÀN PHÍM (KEY DISPATCH)
// =========================================================================
static bool mode_phonics_handle_key(uint8_t kc, uint8_t mod, char c) {
    (void)mod;
    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // =========================================================================
    // 1. PHÍM ĐIỀU PHỐI 3 CHẾ ĐỘ: CAPS LOCK / NUM LOCK / ESC
    // =========================================================================

    // Phím CAPS LOCK (0x39): Chuyển sang / Bật tắt TRÒ CHƠI BÉ TẬP ĐÁNH VẦN
    if (kc == 0x39) {
        if (s_submode != SUBMODE_SPELLING) {
            s_submode = SUBMODE_SPELLING;
            s_math_active = false;
            s_math_state = MATH_STATE_IDLE;
            s_math_timer_ms = 0;
            printf("\r\n============================================================\r\n");
            printf(">>> [CHUYỂN SANG: TRÒ CHƠI BÉ TẬP ĐÁNH VẦN & TÌM CHỮ CÁI] <<<\r\n");
            printf(">>> - Loa đọc câu đố: \"Đố bé tìm thấy Chữ [X] ở đâu nào?\"\r\n");
            printf(">>> - Đèn LED: Nhấp nháy dẫn đường tại vị trí chữ cái trên khung 32 LED!\r\n");
            printf(">>> - Phím chữ A..Z: Bấm để trả lời! (Nếu bấm sai, tự động đọc lại câu hỏi)\r\n");
            printf(">>> (Nhấn CAPS LOCK hoặc ESC để quay lại Chế Độ Gõ Phím Mặc Định)\r\n");
            printf("============================================================\r\n\r\n");
            fflush(stdout);
            led_effects_color_burst(160, 32, 240); // Tím rực rỡ
            sb_play_special_key(0x39); // Đọc to: "Phím Caps Lock"
            spelling_quiz_start_question_delayed(700); // Chờ đọc xong tên phím rồi đọc câu hỏi!
        } else {
            // Đang ở trong chế độ Đánh Vần: Bấm Caps Lock để thoát ra chế độ mặc định
            s_submode = SUBMODE_DEFAULT;
            s_spell_state = SPELL_STATE_IDLE;
            s_spell_timer_ms = 0;
            printf("\r\n>>> [BÉ QUAY LẠI CHẾ ĐỘ MẶC ĐỊNH: GÕ PHÍM ĐỌC TÊN PHÍM] <<<\r\n\r\n");
            fflush(stdout);
            led_effects_color_burst(0, 200, 255);
            sb_play_special_key(0x39);
        }
        return true;
    }

    // Phím NUM LOCK (0x53): Chuyển sang / Bật tắt TRÒ CHƠI BÉ TẬP LÀM TOÁN
    if (kc == 0x53) {
        if (s_submode != SUBMODE_MATH) {
            s_submode = SUBMODE_MATH;
            s_spell_state = SPELL_STATE_IDLE;
            s_spell_timer_ms = 0;
            printf("\r\n============================================================\r\n");
            printf(">>> [CHUYỂN SANG: TRÒ CHƠI BÉ TẬP LÀM TOÁN THẦN TỐC (MATH QUIZ)] <<<\r\n");
            printf(">>> - Loa đọc câu đố: \"A cộng/trừ B bằng mấy?\"\r\n");
            printf(">>> - Đèn LED 4 cạnh: Trái (A) - Phải (B) - Trên (+/-) - Dưới (Đáp án)\r\n");
            printf(">>> - Phím số 0..9: Bấm đáp án (Đọc to: \"Bằng X! Chính xác, bé giỏi quá!\")\r\n");
            printf(">>> (Nhấn NUM LOCK hoặc ESC để quay lại Chế Độ Gõ Phím Mặc Định)\r\n");
            printf("============================================================\r\n\r\n");
            fflush(stdout);
            led_effects_color_burst(255, 215, 0); // Vàng kim rực rỡ
            sb_play_special_key(0x53); // Đọc to: "Phím Num Lock"
            math_quiz_start_question_delayed(700); // Chờ đọc xong tên phím rồi đọc câu hỏi!
        } else {
            // Đang ở trong chế độ Làm Toán: Bấm Num Lock để thoát ra chế độ mặc định
            s_submode = SUBMODE_DEFAULT;
            s_math_active = false;
            s_math_state = MATH_STATE_IDLE;
            s_math_timer_ms = 0;
            printf("\r\n>>> [BÉ QUAY LẠI CHẾ ĐỘ MẶC ĐỊNH: GÕ PHÍM ĐỌC TÊN PHÍM] <<<\r\n\r\n");
            fflush(stdout);
            led_effects_color_burst(0, 200, 255);
            sb_play_special_key(0x53);
        }
        return true;
    }

    // Phím ESC (0x29): Trở về Chế độ Mặc định (Gõ tự do) từ bất kỳ trò chơi nào
    if (kc == 0x29) {
        if (s_submode != SUBMODE_DEFAULT) {
            s_submode = SUBMODE_DEFAULT;
            s_math_active = false;
            s_math_state = MATH_STATE_IDLE;
            s_math_timer_ms = 0;
            s_spell_state = SPELL_STATE_IDLE;
            s_spell_timer_ms = 0;
            printf("\r\n>>> [BÉ QUAY LẠI CHẾ ĐỘ MẶC ĐỊNH: GÕ PHÍM ĐỌC TÊN PHÍM] <<<\r\n\r\n");
            fflush(stdout);
            led_effects_color_burst(0, 200, 255);
            sb_play_special_key(0x29); // Đọc to: "Phím Escape"
            return true;
        }
        sb_play_special_key(0x29);
        return true;
    }

    // =========================================================================
    // 2. KHI ĐANG Ở TRÒ CHƠI BÉ TẬP ĐÁNH VẦN (SUBMODE_SPELLING)
    // =========================================================================
    if (s_submode == SUBMODE_SPELLING) {
        // Phím Space hoặc '?' : Phát lại câu hỏi cho bé nghe
        if (c == ' ' || kc == 0x2C || c == '?') {
            s_spell_state = SPELL_STATE_SPEAK_INTRO;
            s_spell_timer_ms = 0;
            printf("\r\n>>> [PHÁT LẠI CÂU ĐỐ]: Đố bé tìm thấy chữ '%c' ở đâu nào? <<<\r\n", s_spell_target);
            return true;
        }

        // Phím '+', '-', '=': Bỏ qua câu khó, chuyển sang câu đố chữ mới
        if (c == '+' || c == '-' || c == '=') {
            spelling_quiz_start_question();
            return true;
        }

        // Bé bấm phím chữ cái (A..Z) để trả lời
        if (upper >= 'A' && upper <= 'Z') {
            // Cho phép bé trả lời ngay cả khi câu hỏi đang đọc hoặc khi đang chờ bấm
            if (s_spell_state != SPELL_STATE_CORRECT_PRAISE && s_spell_state != SPELL_STATE_HINT_WRONG) {
                s_spell_user_guess = upper;
                if (upper == s_spell_target) {
                    s_spell_score += 10;
                    s_spell_streak++;
                    printf("\r\n============================================================\r\n");
                    printf(">>> [CHÍNH XÁC! BÉ GIỎI QUÁ!]: Chữ '%c'!\r\n", upper);
                    printf(">>> [TỪ GỢI Ý]: %s\r\n", s_spell_dict[upper - 'A']);
                    printf(">>> [ĐIỂM SỐ: %d ĐIỂM | CHUỖI ĐÚNG LIÊN TIẾP (STREAK): %d] <<<\r\n",
                           s_spell_score, s_spell_streak);
                    printf("============================================================\r\n\r\n");
                    fflush(stdout);

                    led_effects_celebration();
                    led_effects_spelling_quiz_display(s_spell_target, upper, false);

                    uint32_t dur = 1400;
                    math_audio_play(MATH_SND_PRAISE_CORRECT, &dur);
                    s_spell_state = SPELL_STATE_CORRECT_PRAISE;
                    s_spell_timer_ms = dur + 400; // Đợi đọc xong lời khen rồi sang câu tiếp theo
                } else {
                    s_spell_streak = 0;
                    printf(" -> [CHƯA ĐÚNG RỒI]: Bé bấm chữ '%c'. Không phải chữ '%c' đâu!\r\n",
                           upper, s_spell_target);
                    printf(" -> [GỢI Ý]: Đèn LED đang nhấp nháy dẫn đường tại vị trí chữ '%c' đó!\r\n", s_spell_target);
                    printf(" -> [TỰ ĐỘNG ĐỌC LẠI CÂU HỎI]: \"Đố bé tìm thấy chữ %c ở đâu nào?\"\r\n", s_spell_target);
                    fflush(stdout);

                    uint32_t dur = 1600;
                    math_audio_play(MATH_SND_SPELL_HINT_WRONG, &dur); // "Chưa đúng rồi, bé tìm lại nhé!"
                    led_effects_spelling_quiz_display(s_spell_target, upper, false);
                    s_spell_state = SPELL_STATE_HINT_WRONG;
                    s_spell_timer_ms = dur + 250; // Chờ đọc xong lời nhắc rồi tự động đọc lại câu hỏi
                }
            }
            return true;
        }

        // Trong Chế độ Đánh Vần, các phím khác không được chạy đè âm thanh
        return true;
    }

    // =========================================================================
    // 3. KHI ĐANG Ở TRÒ CHƠI BÉ TẬP LÀM TOÁN (SUBMODE_MATH)
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

        // Phím '+' hoặc '-' hoặc '=': Chuyển sang câu đố mới
        if (c == '+' || c == '-' || c == '=') {
            math_quiz_start_question();
            return true;
        }

        // Nhận diện phím số bé bấm (0..9) làm đáp án
        int8_t ans = -1;
        if (c >= '0' && c <= '9') ans = (int8_t)(c - '0');
        else if (kc >= 0x59 && kc <= 0x61) ans = (int8_t)(kc - 0x59 + 1); // Numpad 1..9
        else if (kc == 0x62) ans = 0; // Numpad 0

        if (ans >= 0) {
            // Cho phép bé trả lời ngay cả khi câu hỏi đang đọc hoặc khi đang chờ bấm
            if (s_math_state != MATH_STATE_ANS_BANG &&
                s_math_state != MATH_STATE_ANS_VAL &&
                s_math_state != MATH_STATE_ANS_PRAISE &&
                s_math_state != MATH_STATE_CELEBRATE &&
                s_math_state != MATH_STATE_HINT_WRONG) {
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
                    printf(" -> [TỰ ĐỘNG ĐỌC LẠI CÂU HỎI]: \"%s %s %s bằng mấy?\"\r\n",
                           s_math_num_words[s_math_a <= 10 ? s_math_a : 0],
                           (s_math_op == 0 ? "cộng" : "trừ"),
                           s_math_num_words[s_math_b <= 10 ? s_math_b : 0]);
                    fflush(stdout);

                    uint32_t dur = 600;
                    math_audio_play(MATH_SND_HINT_WRONG, &dur);
                    led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, ans, false);
                    s_math_state = MATH_STATE_HINT_WRONG;
                    s_math_timer_ms = dur + 250; // Chờ đọc xong rồi tự động đọc lại câu hỏi!
                }
            }
            return true;
        }

        // Khi đang ở Trò chơi làm toán, các phím khác không được chạy đè âm thanh
        return true;
    }

    // =========================================================================
    // 4. KHI ĐANG Ở CHẾ ĐỘ MẶC ĐỊNH: GÕ PHÍM ĐỌC TÊN PHÍM (SUBMODE_DEFAULT)
    // =========================================================================

    // 1. Phím chữ cái A - Z: Phát âm ngữ âm tiếng Việt & thắp sáng vị trí chữ cái trên khung vuông
    if (upper >= 'A' && upper <= 'Z') {
        audio_play_phonics_letter(upper);
        led_effects_phonics_letter(upper);

        const char *spell_text = s_spell_dict[upper - 'A'];

        // Ghép vần 2 chữ cái cơ bản: Phụ âm + Nguyên âm (A, E, O, U, I)
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

    // 6. Phím chức năng đặc biệt (F1..F9, Space, Enter, Backspace, Tab, Shift, Ctrl, Alt...)
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

// =========================================================================
// BACKGROUND TICK: TIẾN TRÌNH STATE MACHINE ÂM THANH & LED
// =========================================================================
static void mode_phonics_background_tick(uint32_t delta_ms) {
    if (s_submode == SUBMODE_DEFAULT) return;

    // Timer nhấp nháy đèn dẫn đường
    s_pulse_timer += delta_ms;
    if (s_pulse_timer >= 280) {
        s_pulse_timer = 0;
        s_pulse_toggle = !s_pulse_toggle;
    }

    uint32_t dur = 300;

    // -------------------------------------------------------------------------
    // A. STATE MACHINE TRÒ CHƠI BÉ TẬP ĐÁNH VẦN (SUBMODE_SPELLING)
    // -------------------------------------------------------------------------
    if (s_submode == SUBMODE_SPELLING) {
        if (s_spell_timer_ms > delta_ms) {
            s_spell_timer_ms -= delta_ms;
        } else {
            s_spell_timer_ms = 0;
        }

        switch (s_spell_state) {
            case SPELL_STATE_SPEAK_INTRO:
                if (s_spell_timer_ms == 0) {
                    math_audio_play(MATH_SND_SPELL_INTRO, &dur); // "Đố bé tìm thấy"
                    s_spell_state = SPELL_STATE_SPEAK_LETTER;
                    s_spell_timer_ms = dur + 40;
                }
                break;

            case SPELL_STATE_SPEAK_LETTER:
                if (s_spell_timer_ms == 0) {
                    sb_play_letter_dur(s_spell_target, &dur); // "Chữ X"
                    s_spell_state = SPELL_STATE_SPEAK_OUTRO;
                    s_spell_timer_ms = dur + 40;
                }
                break;

            case SPELL_STATE_SPEAK_OUTRO:
                if (s_spell_timer_ms == 0) {
                    math_audio_play(MATH_SND_SPELL_OUTRO, &dur); // "ở đâu nào?"
                    s_spell_state = SPELL_STATE_WAIT_INPUT;
                    s_spell_timer_ms = dur + 60;
                }
                break;

            case SPELL_STATE_WAIT_INPUT:
                led_effects_spelling_quiz_display(s_spell_target, s_spell_user_guess, s_pulse_toggle);
                break;

            case SPELL_STATE_CORRECT_PRAISE:
                if (s_spell_timer_ms == 0) {
                    spelling_quiz_start_question_delayed(400);
                }
                break;

            case SPELL_STATE_HINT_WRONG:
                if (s_spell_timer_ms == 0) {
                    // Sau khi nhắc nhở sai xong, tự động đọc lại câu hỏi từ đầu!
                    s_spell_user_guess = 0;
                    led_effects_spelling_quiz_display(s_spell_target, 0, true);
                    s_spell_state = SPELL_STATE_SPEAK_INTRO;
                    s_spell_timer_ms = 0;
                }
                break;

            default:
                break;
        }
        return;
    }

    // -------------------------------------------------------------------------
    // B. STATE MACHINE TRÒ CHƠI BÉ TẬP LÀM TOÁN (SUBMODE_MATH)
    // -------------------------------------------------------------------------
    if (s_submode == SUBMODE_MATH && s_math_active) {
        if (s_math_timer_ms > delta_ms) {
            s_math_timer_ms -= delta_ms;
        } else {
            s_math_timer_ms = 0;
        }

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
                led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, s_math_user_ans, s_pulse_toggle);
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

            case MATH_STATE_HINT_WRONG:
                if (s_math_timer_ms == 0) {
                    // Sau khi nhắc nhở xong, tự động đọc lại câu hỏi từ đầu: "A op B bằng mấy?"
                    s_math_user_ans = -1;
                    led_effects_math_quiz_display(s_math_a, s_math_op, s_math_b, -1, true);
                    s_math_state = MATH_STATE_SPEAK_A;
                    s_math_timer_ms = 0;
                }
                break;

            default:
                break;
        }
    }
}

// =========================================================================
// ACTION BUTTON (NÚT BẤM CỨNG TRÊN MẠCH HOẶC TRONG APP)
// =========================================================================
static void mode_phonics_handle_action(void) {
    if (s_submode == SUBMODE_SPELLING) {
        // Đang ở Đánh Vần: bấm Action đổi câu đố chữ mới
        spelling_quiz_start_question();
    } else if (s_submode == SUBMODE_MATH) {
        // Đang ở Làm Toán: bấm Action đổi câu đố toán mới
        math_quiz_start_question();
    } else {
        // Đang ở chế độ mặc định: bấm Action chuyển sang Trò chơi Đánh vần (tương đương Caps Lock)
        s_submode = SUBMODE_SPELLING;
        led_effects_color_burst(160, 32, 240);
        sb_play_special_key(0x39);
        spelling_quiz_start_question_delayed(700);
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
