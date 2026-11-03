/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "errno.h"
#include "driver/gpio.h"

#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"

#include "audio_synth.h"
#include "led_effects.h"
#include "rotary_encoder.h"
#include "soundbank.h"

static const char *TAG = "usb_toy";

QueueHandle_t app_event_queue = NULL;

typedef enum {
    APP_EVENT_HID_HOST = 0,
    APP_EVENT_UPDATE_LEDS,
    APP_EVENT_ENCODER_ROTATE,
    APP_EVENT_ENCODER_CLICK
} app_event_group_t;

typedef struct {
    app_event_group_t event_group;
    union {
        struct {
            hid_host_device_handle_t handle;
            hid_host_driver_event_t event;
            void *arg;
        } hid_host_device;
        int encoder_delta;
    };
} app_event_queue_t;

static const char *hid_proto_name_str[] = {
    "NONE",
    "KEYBOARD",
    "MOUSE"
};

typedef struct {
    enum key_state {
        KEY_STATE_PRESSED = 0x00,
        KEY_STATE_RELEASED = 0x01
    } state;
    uint8_t modifier;
    uint8_t key_code;
} key_event_t;

#define KEYBOARD_ENTER_MAIN_CHAR    '\r'
#define KEYBOARD_ENTER_LF_EXTEND    1

const uint8_t keycode2ascii [57][2] = {
    {0, 0}, /* HID_KEY_NO_PRESS        */
    {0, 0}, /* HID_KEY_ROLLOVER        */
    {0, 0}, /* HID_KEY_POST_FAIL       */
    {0, 0}, /* HID_KEY_ERROR_UNDEFINED */
    {'a', 'A'}, /* HID_KEY_A               */
    {'b', 'B'}, /* HID_KEY_B               */
    {'c', 'C'}, /* HID_KEY_C               */
    {'d', 'D'}, /* HID_KEY_D               */
    {'e', 'E'}, /* HID_KEY_E               */
    {'f', 'F'}, /* HID_KEY_F               */
    {'g', 'G'}, /* HID_KEY_G               */
    {'h', 'H'}, /* HID_KEY_H               */
    {'i', 'I'}, /* HID_KEY_I               */
    {'j', 'J'}, /* HID_KEY_J               */
    {'k', 'K'}, /* HID_KEY_K               */
    {'l', 'L'}, /* HID_KEY_L               */
    {'m', 'M'}, /* HID_KEY_M               */
    {'n', 'N'}, /* HID_KEY_N               */
    {'o', 'O'}, /* HID_KEY_O               */
    {'p', 'P'}, /* HID_KEY_P               */
    {'q', 'Q'}, /* HID_KEY_Q               */
    {'r', 'R'}, /* HID_KEY_R               */
    {'s', 'S'}, /* HID_KEY_S               */
    {'t', 'T'}, /* HID_KEY_T               */
    {'u', 'U'}, /* HID_KEY_U               */
    {'v', 'V'}, /* HID_KEY_V               */
    {'w', 'W'}, /* HID_KEY_W               */
    {'x', 'X'}, /* HID_KEY_X               */
    {'y', 'Y'}, /* HID_KEY_Y               */
    {'z', 'Z'}, /* HID_KEY_Z               */
    {'1', '!'}, /* HID_KEY_1               */
    {'2', '@'}, /* HID_KEY_2               */
    {'3', '#'}, /* HID_KEY_3               */
    {'4', '$'}, /* HID_KEY_4               */
    {'5', '%'}, /* HID_KEY_5               */
    {'6', '^'}, /* HID_KEY_6               */
    {'7', '&'}, /* HID_KEY_7               */
    {'8', '*'}, /* HID_KEY_8               */
    {'9', '('}, /* HID_KEY_9               */
    {'0', ')'}, /* HID_KEY_0               */
    {KEYBOARD_ENTER_MAIN_CHAR, KEYBOARD_ENTER_MAIN_CHAR}, /* HID_KEY_ENTER           */
    {0, 0}, /* HID_KEY_ESC             */
    {'\b', '\b'}, /* HID_KEY_DEL (Backspace) */
    {'\t', '\t'}, /* HID_KEY_TAB             */
    {' ', ' '}, /* HID_KEY_SPACE           */
    {'-', '_'}, /* HID_KEY_MINUS           */
    {'=', '+'}, /* HID_KEY_EQUAL           */
    {'[', '{'}, /* HID_KEY_OPEN_BRACKET    */
    {']', '}'}, /* HID_KEY_CLOSE_BRACKET   */
    {'\\', '|'}, /* HID_KEY_BACK_SLASH      */
    {'\\', '|'}, /* HID_KEY_SHARP           */
    {';', ':'}, /* HID_KEY_COLON           */
    {'\'', '"'}, /* HID_KEY_QUOTE           */
    {'`', '~'}, /* HID_KEY_TILDE           */
    {',', '<'}, /* HID_KEY_LESS            */
    {'.', '>'}, /* HID_KEY_GREATER         */
    {'/', '?'} /* HID_KEY_SLASH           */
};

// -------------------------------------------------------------
// 8-MODE STATE MACHINE DEFINITIONS
// -------------------------------------------------------------
typedef enum {
    MODE_1_PIANO = 0,
    MODE_2_BABY_MOZART,
    MODE_3_SPEED_RUNNER,
    MODE_4_KIDS_DRUMMER,
    MODE_5_TYPEWRITER,
    MODE_6_CARTOON_SFX,
    MODE_7_SECRET_AGENT_ROBOT,
    MODE_8_COCKPIT_ANIMALS,
    MODE_9_LEARN_PHONICS_NUMBERS,
    MODE_MAX_COUNT
} toy_mode_t;

static const char *s_mode_names[MODE_MAX_COUNT] = {
    "1. ĐÀN PIANO DIỆU KỲ & NGŨ CUNG (FULL TOÀN BÀN PHÍM)",
    "2. NGHỆ SĨ THIÊN TÀI (BABY MOZART)",
    "3. NHẠC CHẠY THEO TỐC ĐỘ TAY",
    "4. DÀN TRỐNG BEAT-MAKER & DJ TOÀN DẢI",
    "5. MÁY GÕ CHỮ CỔ ĐIỂN",
    "6. HỘP ÂM THANH GAME & HOẠT HÌNH",
    "7. ĐIỆP VIÊN NHÍ & ROBOT R2-D2",
    "8. BUỒNG LÁI XE CỨU HỎA & 26 CON VẬT",
    "9. BÉ HỌC ĐÁNH VẦN CHỮ CÁI & TẬP ĐẾM SỐ (PHONICS & COUNTING)"
};

static toy_mode_t s_current_mode = MODE_1_PIANO;
static piano_timbre_t s_piano_timbre = TIMBRE_PIANO;
static bool s_pentatonic_mode = false;

// Mode 6: Cartoon & Arcade SFX State
static uint8_t s_cartoon_scenario_step = 0;
static uint16_t s_cartoon_combo = 0;
static int64_t s_cartoon_last_key_time = 0;

// Hardware LEDs on keyboard
static hid_host_device_handle_t s_kb_handle = NULL;
static bool s_caps_lock = false;
static bool s_num_lock = true;
static bool s_scroll_lock = false;

static inline void trigger_led_update(void) {
    if (app_event_queue) {
        app_event_queue_t evt = { .event_group = APP_EVENT_UPDATE_LEDS };
        xQueueSend(app_event_queue, &evt, 0);
    }
}

static void update_keyboard_leds(void) {
    if (s_kb_handle == NULL) return;
    uint8_t led_report = 0;
    if (s_num_lock)    led_report |= 0x01;
    if (s_caps_lock)   led_report |= 0x02;
    if (s_scroll_lock) led_report |= 0x04;

    hid_class_request_set_report(s_kb_handle,
                                 HID_REPORT_TYPE_OUTPUT,
                                 0,
                                 &led_report,
                                 1);
}

static inline bool hid_keyboard_is_modifier_shift(uint8_t modifier) {
    return (((modifier & 0x02) == 0x02) || ((modifier & 0x20) == 0x20));
}

static void switch_toy_mode(toy_mode_t new_mode) {
    s_current_mode = new_mode % MODE_MAX_COUNT;
    printf("\r\n\r\n========================================================\r\n");
    printf(">>> [CHUYỂN CHẾ ĐỘ: %s] <<<\r\n", s_mode_names[s_current_mode]);
    printf("========================================================\r\n");
    fflush(stdout);

    audio_synth_stop_all_background();
    audio_mozart_set_autoplay(false);
    soundbank_set_mode(s_current_mode);
    if (!sb_play_mode_name(s_current_mode)) {
        if (!sb_play_chime(s_current_mode)) {
            audio_play_chime_mode(s_current_mode);
        }
    }
    led_effects_mode_switch(s_current_mode);

    if (s_current_mode == MODE_2_BABY_MOZART) {
        mozart_play_info_t info;
        audio_mozart_get_info(&info);
        printf("\r\n>>> [BABY MOZART READY: 20 Tuyệt Phẩm Cổ Điển & Thiếu Nhi] <<<\r\n");
        printf(">>> [Bài mở đầu: \"%s\" - %s (%d nốt)] <<<\r\n", info.name, info.artist, (int)info.total_notes);
        printf(">>> [Phím tắt: Space/Mũi tên = Đổi bài | Tab/T = Đổi nhạc cụ | Enter/P = Hát Ru Auto-Play | 1..9 = Chọn bài] <<<\r\n\r\n");
    } else if (s_current_mode == MODE_6_CARTOON_SFX) {
        s_cartoon_scenario_step = 0;
        s_cartoon_combo = 0;
        printf("\r\n>>> [VŨ TRỤ GAME & PHIM HOẠT HÌNH: 34 ÂM THANH KINH ĐIỂN] <<<\r\n");
        printf(">>> [1..0/Numpad = Mario & Arcade | QWERTY = Action Hero | ASDFGH = Tom & Jerry Slapstick | ZXCVBNM = Phép Thuật] <<<\r\n");
        printf(">>> [Phím Space = Vở kịch rượt đuổi Tom & Jerry | Backspace = Troll Trombone | Enter = Fanfare Chiến Thắng] <<<\r\n");
        printf(">>> [Gõ phím liên tục tạo COMBO x5, COMBO x10 nhận phần thưởng âm thanh & đèn LED rực rỡ!] <<<\r\n\r\n");
    }
}

static const char *s_morse_letters[26] = {
    ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....", "..",
    ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.", "--.-", ".-.",
    "...",  "-",    "..-",  "...-", ".--",  "-..-", "-.--", "--.."
};

static const char *s_morse_digits[10] = {
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
};

// -------------------------------------------------------------
// TOY KEY EVENT HANDLER (FULL 100% KEYBOARD COVERAGE)
// -------------------------------------------------------------
static void handle_toy_mode_key(uint8_t kc, uint8_t mod, char c) {
    // 1. GLOBAL SYSTEM SHORTCUTS
    if (kc == 0x29) { // ESC: Next mode
        switch_toy_mode((toy_mode_t)((s_current_mode + 1) % MODE_MAX_COUNT));
        return;
    }
    if (kc >= 0x3A && kc <= 0x42) { // F1 - F9: Direct jump to Modes 1 - 9
        switch_toy_mode((toy_mode_t)(kc - 0x3A));
        return;
    }
    if (kc == 0x44) { // F11: Vol down
        audio_synth_volume_down();
        printf(" -> [GIẢM ÂM LƯỢNG: %d%%]\r\n", (int)(audio_synth_get_volume() * 100));
        audio_play_volume_tick(audio_synth_get_volume(), false);
        led_effects_volume(audio_synth_get_volume());
        return;
    }
    if (kc == 0x45) { // F12: Vol up
        audio_synth_volume_up();
        printf(" -> [TĂNG ÂM LƯỢNG: %d%%]\r\n", (int)(audio_synth_get_volume() * 100));
        audio_play_volume_tick(audio_synth_get_volume(), true);
        led_effects_volume(audio_synth_get_volume());
        return;
    }

    char upper = 0;
    if (c >= 'a' && c <= 'z') upper = c - 32;
    else if (c >= 'A' && c <= 'Z') upper = c;

    // 2. DISPATCH EVENT BASED ON CURRENT MODE
    switch (s_current_mode) {

        // =====================================================
        // MODE 1: ĐÀN PIANO DIỆU KỲ & NGŨ CUNG (FULL 100% TOÀN BỘ PHÍM)
        // =====================================================
        case MODE_1_PIANO: {
            if (kc == 0x2B) { // TAB: Bật / tắt thang âm ngũ cung
                s_pentatonic_mode = !s_pentatonic_mode;
                printf(" -> [THANG ÂM NGŨ CUNG (PENTATONIC): %s]\r\n", s_pentatonic_mode ? "BẬT (CHỐNG PHÔ)" : "TẮT (CHUẨN 7 NỐT)");
                audio_play_cartoon_sfx(SFX_MARIO_COIN);
                led_effects_color_burst(255, 215, 0);
                return;
            }
            if (kc == 0x43) { // F10: Luân chuyển âm sắc (Piano trầm ấm -> Retro 8-bit -> Chuông gió Music Box)
                s_piano_timbre = (piano_timbre_t)((s_piano_timbre + 1) % 3);
                const char *t_name = (s_piano_timbre == TIMBRE_PIANO) ? "ĐÀN PIANO TRẦM ẤM" :
                                     (s_piano_timbre == TIMBRE_8BIT_MARIO) ? "RETRO GAME 8-BIT MARIO" : "CHUÔNG GIÓ MUSIC BOX";
                printf(" -> [ĐỔI ÂM SẮC: %s]\r\n", t_name);
                audio_play_cartoon_sfx(SFX_MARIO_COIN);
                led_effects_color_burst(0, 255, 255);
                return;
            }

            float freq = 0.0f;
            int led_idx = 0;
            const char *note_name = NULL;

            // 1. HÀNG SỐ TRÊN (14 phím: ~ .. 0, -, =, Backspace)
            if (kc == 0x35 || c == '`' || c == '~')      { freq = 493.88f; note_name = "Si (B4)"; led_idx = 0; }
            else if (kc == 0x1E || c == '1' || c == '!') { freq = 523.25f; note_name = "Đố (C5)"; led_idx = 1; }
            else if (kc == 0x1F || c == '2' || c == '@') { freq = 587.33f; note_name = "Rế (D5)"; led_idx = 2; }
            else if (kc == 0x20 || c == '3' || c == '#') { freq = 659.25f; note_name = "Mí (E5)"; led_idx = 3; }
            else if (kc == 0x21 || c == '4' || c == '$') { freq = 698.46f; note_name = "Phá (F5)"; led_idx = 4; }
            else if (kc == 0x22 || c == '5' || c == '%') { freq = 783.99f; note_name = "Són (G5)"; led_idx = 5; }
            else if (kc == 0x23 || c == '6' || c == '^') { freq = 880.00f; note_name = "Lá (A5)"; led_idx = 6; }
            else if (kc == 0x24 || c == '7' || c == '&') { freq = 987.77f; note_name = "Sí (B5)"; led_idx = 7; }
            else if (kc == 0x25 || c == '8' || c == '*') { freq = 1046.50f; note_name = "Đố (C6)"; led_idx = 8; }
            else if (kc == 0x26 || c == '9' || c == '(') { freq = 1174.66f; note_name = "Rế (D6)"; led_idx = 9; }
            else if (kc == 0x27 || c == '0' || c == ')') { freq = 1318.51f; note_name = "Mí (E6)"; led_idx = 10; }
            else if (kc == 0x2D || c == '-' || c == '_') { freq = 1396.91f; note_name = "Phá (F6)"; led_idx = 11; }
            else if (kc == 0x2E || c == '=' || c == '+') { freq = 1567.98f; note_name = "Són (G6)"; led_idx = 12; }
            else if (kc == 0x2A || c == '\b')            { freq = 1760.00f; note_name = "Lá (A6)"; led_idx = 13; }

            // 2. HÀNG QWERTY (Quãng 5 Bổng & Phím đen C#4 -> C6)
            else if (kc == 0x14 || upper == 'Q') { freq = 523.25f; note_name = "Đố (C5 Bổng)"; led_idx = 0; }
            else if (kc == 0x1A || upper == 'W') { freq = 277.18f; note_name = "Đồ# (C#4)"; led_idx = 1; }
            else if (kc == 0x08 || upper == 'E') { freq = 311.13f; note_name = "Rê# (D#4)"; led_idx = 2; }
            else if (kc == 0x15 || upper == 'R') { freq = 698.46f; note_name = "Phá (F5)"; led_idx = 3; }
            else if (kc == 0x17 || upper == 'T') { freq = 369.99f; note_name = "Pha# (F#4)"; led_idx = 4; }
            else if (kc == 0x1C || upper == 'Y') { freq = 415.30f; note_name = "Son# (G#4)"; led_idx = 5; }
            else if (kc == 0x18 || upper == 'U') { freq = 466.16f; note_name = "La# (A#4)"; led_idx = 6; }
            else if (kc == 0x0C || upper == 'I') { freq = 783.99f; note_name = "Són (G5)"; led_idx = 7; }
            else if (kc == 0x12 || upper == 'O') { freq = 554.37f; note_name = "Đố# (C#5)"; led_idx = 8; }
            else if (kc == 0x13 || upper == 'P') { freq = 622.25f; note_name = "Rế# (D#5)"; led_idx = 9; }
            else if (kc == 0x2F || c == '[' || c == '{') { freq = 880.00f; note_name = "Lá (A5)"; led_idx = 10; }
            else if (kc == 0x30 || c == ']' || c == '}') { freq = 987.77f; note_name = "Sí (B5)"; led_idx = 11; }
            else if (kc == 0x31 || c == '\\' || c == '|' || kc == 0x32) { freq = 1046.50f; note_name = "Đố (C6 Siêu Cao)"; led_idx = 12; }

            // 3. HÀNG ASDF (Quãng 4 Trung chuẩn, gam Đô Trưởng / Ngũ Cung)
            else if (kc == 0x39)                 { freq = 65.41f;  note_name = "Caps: Đồ Siêu Trầm (C2)"; led_idx = 0; }
            else if (kc == 0x04 || upper == 'A') { freq = 261.63f; note_name = "Đồ (C4)"; led_idx = 0; }
            else if (kc == 0x16 || upper == 'S') { freq = 293.66f; note_name = "Rê (D4)"; led_idx = 1; }
            else if (kc == 0x07 || upper == 'D') { freq = 329.63f; note_name = "Mi (E4)"; led_idx = 2; }
            else if (kc == 0x09 || upper == 'F') { freq = s_pentatonic_mode ? 392.00f : 349.23f; note_name = s_pentatonic_mode ? "Son (G4 Ngũ Cung)" : "Pha (F4)"; led_idx = 3; }
            else if (kc == 0x0A || upper == 'G') { freq = s_pentatonic_mode ? 440.00f : 392.00f; note_name = s_pentatonic_mode ? "La (A4 Ngũ Cung)" : "Son (G4)"; led_idx = 4; }
            else if (kc == 0x0B || upper == 'H') { freq = s_pentatonic_mode ? 523.25f : 440.00f; note_name = s_pentatonic_mode ? "Đố (C5 Ngũ Cung)" : "La (A4)"; led_idx = 5; }
            else if (kc == 0x0D || upper == 'J') { freq = s_pentatonic_mode ? 587.33f : 493.88f; note_name = s_pentatonic_mode ? "Rế (D5 Ngũ Cung)" : "Si (B4)"; led_idx = 6; }
            else if (kc == 0x0E || upper == 'K') { freq = s_pentatonic_mode ? 659.25f : 523.25f; note_name = s_pentatonic_mode ? "Mí (E5 Ngũ Cung)" : "Đố (C5)"; led_idx = 7; }
            else if (kc == 0x0F || upper == 'L') { freq = s_pentatonic_mode ? 783.99f : 587.33f; note_name = s_pentatonic_mode ? "Són (G5 Ngũ Cung)" : "Rế (D5)"; led_idx = 8; }
            else if (kc == 0x33 || c == ';' || c == ':') { freq = s_pentatonic_mode ? 880.00f : 659.25f; note_name = s_pentatonic_mode ? "Lá (A5 Ngũ Cung)" : "Mí (E5)"; led_idx = 9; }
            else if (kc == 0x34 || c == '\'' || c == '"') { freq = 698.46f; note_name = "Phá (F5)"; led_idx = 10; }
            else if (kc == 0x28 || c == '\r' || c == '\n') { freq = 523.25f; note_name = "Enter: Hợp Âm Kết Thúc"; led_idx = 15; }

            // 4. HÀNG ZXCV (Quãng 3 Trầm Ấm)
            else if (kc == 0xE1)                 { freq = 130.81f; note_name = "Shift Trái: Đồ (C3)"; led_idx = 0; }
            else if (kc == 0x1D || upper == 'Z') { freq = 130.81f; note_name = "Đồ (C3 Trầm)"; led_idx = 0; }
            else if (kc == 0x1B || upper == 'X') { freq = 146.83f; note_name = "Rê (D3 Trầm)"; led_idx = 1; }
            else if (kc == 0x06 || upper == 'C') { freq = 164.81f; note_name = "Mi (E3 Trầm)"; led_idx = 2; }
            else if (kc == 0x19 || upper == 'V') { freq = 174.61f; note_name = "Pha (F3 Trầm)"; led_idx = 3; }
            else if (kc == 0x05 || upper == 'B') { freq = 196.00f; note_name = "Son (G3 Trầm)"; led_idx = 4; }
            else if (kc == 0x11 || upper == 'N') { freq = 220.00f; note_name = "La (A3 Trầm)"; led_idx = 5; }
            else if (kc == 0x10 || upper == 'M') { freq = 246.94f; note_name = "Si (B3 Trầm)"; led_idx = 6; }
            else if (kc == 0x36 || c == ',' || c == '<') { freq = 261.63f; note_name = "Đồ (C4)"; led_idx = 7; }
            else if (kc == 0x37 || c == '.' || c == '>') { freq = 293.66f; note_name = "Rê (D4)"; led_idx = 8; }
            else if (kc == 0x38 || c == '/' || c == '?') { freq = 329.63f; note_name = "Mi (E4)"; led_idx = 9; }
            else if (kc == 0xE5)                 { freq = 349.23f; note_name = "Shift Phải: Pha (F4)"; led_idx = 10; }

            // 5. CỤM PHÍM SỐ NUMPAD BÊN PHẢI (0..9, +, -, *, /, .)
            else if (kc >= 0x54 && kc <= 0x63) {
                switch (kc) {
                    case 0x59: freq = 261.63f; note_name = "Numpad 1: Đồ (C4)"; led_idx = 0; break;
                    case 0x5A: freq = 293.66f; note_name = "Numpad 2: Rê (D4)"; led_idx = 2; break;
                    case 0x5B: freq = 329.63f; note_name = "Numpad 3: Mi (E4)"; led_idx = 4; break;
                    case 0x5C: freq = 349.23f; note_name = "Numpad 4: Pha (F4)"; led_idx = 6; break;
                    case 0x5D: freq = 392.00f; note_name = "Numpad 5: Son (G4)"; led_idx = 8; break;
                    case 0x5E: freq = 440.00f; note_name = "Numpad 6: La (A4)"; led_idx = 10; break;
                    case 0x5F: freq = 493.88f; note_name = "Numpad 7: Si (B4)"; led_idx = 12; break;
                    case 0x60: freq = 523.25f; note_name = "Numpad 8: Đố (C5)"; led_idx = 14; break;
                    case 0x61: freq = 587.33f; note_name = "Numpad 9: Rế (D5)"; led_idx = 16; break;
                    case 0x62: freq = 220.00f; note_name = "Numpad 0: La Trầm (A3)"; led_idx = 1; break;
                    case 0x57: freq = 659.25f; note_name = "Numpad +: Mí (E5)"; led_idx = 18; break;
                    case 0x56: freq = 698.46f; note_name = "Numpad -: Phá (F5)"; led_idx = 20; break;
                    case 0x55: freq = 783.99f; note_name = "Numpad *: Són (G5)"; led_idx = 22; break;
                    case 0x54: freq = 880.00f; note_name = "Numpad /: Lá (A5)"; led_idx = 24; break;
                    case 0x63: freq = 1046.50f; note_name = "Numpad .: Đố (C6)"; led_idx = 26; break;
                    case 0x58: freq = 523.25f; note_name = "Numpad Enter: Hợp Âm Đô Trưởng!"; led_idx = 15; break;
                }
            }

            // 6. PHÍM ĐẶC BIỆT, ĐIỀU HƯỚNG & MODIFIERS
            else if (kc == 0x2C || c == ' ') {
                freq = 261.63f; note_name = "Space: Bàn Đạp Ngân Vang (Pedal)"; led_idx = 15;
            } else if (kc >= 0x4F && kc <= 0x52) { // Mũi tên
                freq = 440.0f + (kc - 0x4F) * 110.0f;
                note_name = "Phím Mũi Tên: Lướt Âm Arpeggio";
                led_idx = (kc - 0x4F) * 6;
            } else if (kc == 0xE0 || kc == 0xE4) { // Ctrl
                freq = 130.81f; note_name = "Phím Ctrl: Hợp Âm Trầm Đô"; led_idx = 2;
            } else if (kc == 0xE2 || kc == 0xE6) { // Alt
                freq = 164.81f; note_name = "Phím Alt: Hợp Âm Trầm Mi"; led_idx = 4;
            } else if (kc == 0xE3 || kc == 0xE7) { // Win
                freq = 196.00f; note_name = "Phím Win: Hợp Âm Trầm Son"; led_idx = 6;
            } else { // Các phím còn lại (Insert, Delete, Home, End, PageUp, PageDown, Pause, PrtSc...)
                freq = 300.0f + (kc % 15) * 50.0f;
                note_name = "Phím Chức Năng: Nốt Chuông Nhạc";
                led_idx = kc % 25;
            }

            audio_play_piano_note_key(freq, s_piano_timbre, kc);
            uint8_t r = (led_idx * 23) % 255;
            uint8_t g = (255 - led_idx * 15) % 255;
            uint8_t b = (led_idx * 37) % 255;
            led_effects_note(led_idx, 28, r, g, b);
            printf(" -> [PIANO: %s - %.1f Hz]\r\n", note_name, freq);
            break;
        }

        // =====================================================
        // MODE 2: NGHỆ SĨ THIÊN TÀI (BABY MOZART)
        // =====================================================
        case MODE_2_BABY_MOZART: {
            // 1. Phím Enter hoặc 'P': Bật / Tắt chế độ Auto-Play (Hát ru / Demo)
            if (kc == 0x28 || kc == 0x58 || upper == 'P') {
                audio_mozart_toggle_autoplay();
                bool ap = audio_mozart_is_autoplay();
                printf("\r\n>>> [BABY MOZART: %s CHẾ ĐỘ TỰ ĐỘNG BIỂU DIỄN (LULLABY AUTO-PLAY)] <<<\r\n",
                       ap ? "BẬT" : "TẮT");
                led_effects_color_burst(ap ? 0 : 255, ap ? 200 : 100, ap ? 255 : 50);
                break;
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
            }
            // 3. Phím Mũi tên Trái: Lùi bài trước
            else if (kc == 0x50) {
                audio_mozart_prev_song();
                mozart_play_info_t info;
                audio_mozart_get_info(&info);
                printf("\r\n>>> [BABY MOZART - BÀI TRƯỚC (%d/%d): \"%s\" - %s (%d nốt)] <<<\r\n",
                       (int)(info.song_idx + 1), (int)info.total_songs, info.name, info.artist, (int)info.total_notes);
                led_effects_mode_switch(1);
            }
            // 4. Phím Tab hoặc 'T': Đổi âm sắc nhạc cụ
            else if (kc == 0x2B || upper == 'T') {
                audio_mozart_toggle_timbre();
                printf("\r\n>>> [BABY MOZART: ĐỔI NHẠC CỤ -> %s] <<<\r\n", audio_mozart_get_timbre_name());
                audio_play_cartoon_sfx(SFX_MARIO_COIN);
                led_effects_color_burst(255, 215, 0);
            }
            // 5. Phím 'R': Chơi lại bài hiện tại từ đầu
            else if (upper == 'R') {
                audio_mozart_restart_song();
                mozart_play_info_t info;
                audio_mozart_get_info(&info);
                printf("\r\n>>> [BABY MOZART: CHƠI LẠI TỪ ĐẦU BÀI: \"%s\"] <<<\r\n", info.name);
                led_effects_color_burst(100, 255, 100);
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
            }
            else if (c == '0') {
                audio_mozart_select_song(9); // Bài số 10
                mozart_play_info_t info;
                audio_mozart_get_info(&info);
                printf("\r\n>>> [BABY MOZART - CHỌN BÀI 10/%d: \"%s\" - %s] <<<\r\n",
                       (int)info.total_songs, info.name, info.artist);
                led_effects_mode_switch(1);
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
            }
            break;
        }

        // =====================================================
        // MODE 3: NHẠC CHẠY THEO TỐC ĐỘ TAY (MỌI PHÍM)
        // =====================================================
        case MODE_3_SPEED_RUNNER: {
            audio_speed_runner_keystroke();
            led_effects_beat();
            printf(" -> [SPEED RUNNER: Nhịp nhạc lướt nhanh theo tốc độ gõ!]\r\n");
            break;
        }

        // =====================================================
        // MODE 4: DÀN TRỐNG BEAT-MAKER & DJ (FULL TOÀN BỘ PHÍM)
        // =====================================================
        case MODE_4_KIDS_DRUMMER: {
            // Nhóm Trống Bass Kick (Space, b, z, x, c, v, Numpad 0)
            if (c == ' ' || upper == 'B' || upper == 'Z' || upper == 'X' || upper == 'C' || upper == 'V' || kc == 0x62) {
                audio_play_drum(DRUM_KICK);
                led_effects_beat();
                printf(" -> [DÀN TRỐNG: TRỐNG BASS KICK - BÙM BÙM!]\r\n");
            }
            // Nhóm Trống Snare (s, a, d, f, g, Numpad 1, 2)
            else if (upper == 'S' || upper == 'A' || upper == 'D' || upper == 'F' || upper == 'G' || kc == 0x59 || kc == 0x5A) {
                audio_play_drum(DRUM_SNARE);
                led_effects_color_burst(255, 255, 255);
                printf(" -> [DÀN TRỐNG: TRỐNG CON SNARE - TÁCH CHÁT!]\r\n");
            }
            // Nhóm Chuông Lắc Hi-Hat (h, j, k, l, Numpad 4, 5)
            else if (upper == 'H' || upper == 'J' || upper == 'K' || upper == 'L' || kc == 0x5C || kc == 0x5D) {
                audio_play_drum(DRUM_HIHAT);
                led_effects_color_burst(255, 255, 0);
                printf(" -> [DÀN TRỐNG: CHUÔNG LẮC HI-HAT - XÌ CHÍT!]\r\n");
            }
            // Nhóm Chũm Chọe Cymbal (;, ', c, Numpad 7, 8)
            else if (c == ';' || c == '\'' || kc == 0x5F || kc == 0x60) {
                audio_play_drum(DRUM_CYMBAL);
                led_effects_color_burst(0, 255, 255);
                printf(" -> [DÀN TRỐNG: CHŨM CHỌE CYMBAL - XOANGGG!]\r\n");
            }
            // Nhóm Trống Tom (t, q, w, e, r, y, u, i, o, p, Numpad 3, 6, 9)
            else if (upper == 'T' || upper == 'Q' || upper == 'W' || upper == 'E' || upper == 'R' || upper == 'Y' || upper == 'U' || upper == 'I' || upper == 'O' || upper == 'P' || kc == 0x5B || kc == 0x5E || kc == 0x61) {
                audio_play_drum(DRUM_TOM);
                led_effects_color_burst(255, 128, 0);
                printf(" -> [DÀN TRỐNG: TRỐNG TOM - TÙNG TÙNG!]\r\n");
            }
            // Tiếng vỗ tay Clap (Enter, Numpad Enter)
            else if (c == '\r' || c == '\n' || kc == 0x58) {
                audio_play_drum(DRUM_CLAP);
                led_effects_color_burst(255, 0, 128);
                printf(" -> [DÀN TRỐNG: TIẾNG VỖ TAY HOAN HÔ RÀO RÀO!]\r\n");
            }
            // Tất cả các phím khác (Mũi tên, F-keys, Numpad +, -, *, /)
            else {
                audio_play_drum((drum_type_t)(kc % 6));
                led_effects_spark();
                printf(" -> [DÀN TRỐNG: BỘ GÕ TIẾT TẤU DJ!]\r\n");
            }
            break;
        }

        // =====================================================
        // MODE 5: MÁY GÕ CHỮ CỔ ĐIỂN (FULL TOÀN BỘ PHÍM)
        // =====================================================
        case MODE_5_TYPEWRITER: {
            if (c == ' ') {
                audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
                led_effects_spark();
                printf(" -> [MÁY GÕ CHỮ: KÉO TRỤC GIẤY XOẠTTT]\r\n");
            } else if (c == '\r' || c == '\n' || kc == 0x58) {
                audio_play_typewriter(TYPEWRITER_ENTER_BELL);
                led_effects_color_burst(255, 215, 0);
                printf(" -> [MÁY GÕ CHỮ: CHUÔNG CƠ KHÍ KENGGG!]\r\n");
            } else if (c == '\b' || kc == 0x4C) { // Backspace or Delete
                audio_play_typewriter(TYPEWRITER_BACKSPACE);
                printf(" -> [MÁY GÕ CHỮ: TẨY XÓA ỤT ỊT]\r\n");
            } else if (kc == 0x2B) { // Tab
                audio_play_typewriter(TYPEWRITER_KEY_CLICK);
                printf(" -> [MÁY GÕ CHỮ: THỤT ĐẦU DÒNG CẠCH CẠCH]\r\n");
            } else {
                audio_play_typewriter(TYPEWRITER_KEY_CLICK);
                led_effects_spark();
                printf(" -> [MÁY GÕ CHỮ: BÚA KIM LOẠI TÁCH CẠCH!]\r\n");
            }
            break;
        }

        // =====================================================
        // MODE 6: HỘP ÂM THANH GAME & HOẠT HÌNH (FULL TOÀN PHÍM)
        // =====================================================
        case MODE_6_CARTOON_SFX: {
            int64_t now = esp_timer_get_time();
            if (now - s_cartoon_last_key_time < 1200000LL) {
                s_cartoon_combo++;
            } else {
                s_cartoon_combo = 1;
            }
            s_cartoon_last_key_time = now;

            // Kiểm tra thưởng chuỗi gõ nhanh Combo
            if (s_cartoon_combo == 5) {
                audio_play_cartoon_sfx(SFX_GAME_1UP);
                led_effects_color_burst(255, 215, 0);
                printf("\r\n>>> [COMBO X5! SIÊU CHIẾN BINH HOẠT HÌNH - TẶNG 1-UP!] <<<\r\n");
                return;
            } else if (s_cartoon_combo == 10) {
                audio_play_cartoon_sfx(SFX_VICTORY_FANFARE);
                led_effects_celebration();
                printf("\r\n>>> [COMBO X10 HOÀN HẢO! BÉ LÀ BẬC THẦY GAME ARCADE - PHÁO HOA CHIẾN THẮNG!] <<<\r\n");
                s_cartoon_combo = 0;
                return;
            }

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
                return;
            }

            // 2. Phím BACKSPACE: Troll Sad Trombone
            if (c == '\b' || kc == 0x2A || kc == 0x4C) { // Backspace / Delete
                audio_play_cartoon_sfx(SFX_GAME_OVER_TROMBONE);
                led_effects_color_burst(200, 30, 30);
                printf(" -> [HOẠT HÌNH: TÉ NGÃ THẢM BẠI WAH WAH WAAAH! (SAD TROMBONE)]\r\n");
                return;
            }

            // 3. Phím ENTER: Victory Fanfare
            if (c == '\r' || c == '\n' || kc == 0x28 || kc == 0x58) {
                audio_play_cartoon_sfx(SFX_VICTORY_FANFARE);
                led_effects_celebration();
                printf(" -> [CHIẾN THẮNG KHẢI HOÀN: LEVEL CLEAR FANFARE TƯNG BỪNG!]\r\n");
                return;
            }

            // 4. Phím TAB: Giới thiệu các vùng âm thanh
            if (kc == 0x2B) {
                audio_play_cartoon_sfx(SFX_CT_MAGIC_WAND);
                led_effects_color_burst(180, 50, 255);
                printf(" -> [VŨ TRỤ ÂM THANH: 1..0=Arcade | Q..P=Hero | A..L=Slapstick | Z..M=Magic]\r\n");
                return;
            }

            // 5. Phím Mũi Tên (Arrow Keys)
            if (kc == 0x52) { // Arrow UP: High Jump
                audio_play_cartoon_sfx(SFX_GAME_JUMP);
                led_effects_color_burst(50, 255, 50);
                printf(" -> [MŨI TÊN LÊN: NHẢY BỔNG HIGH JUMP!]\r\n");
                return;
            } else if (kc == 0x51) { // Arrow DOWN: Timpani Boom / Ground Pound
                audio_play_cartoon_sfx(SFX_CT_TIMPANI_BOOM);
                led_effects_color_burst(255, 50, 0);
                printf(" -> [MŨI TÊN XUỐNG: TIẾNG ĐẬP ĐẤT TIMPANI BOOM!]\r\n");
                return;
            } else if (kc == 0x50) { // Arrow LEFT: Phanh gấp
                audio_play_cartoon_sfx(SFX_SKID_STOP);
                led_effects_color_burst(255, 100, 0);
                printf(" -> [MŨI TÊN TRÁI: PHANH XE CHÁY ĐƯỜNG KÉÉÉT!]\r\n");
                return;
            } else if (kc == 0x4F) { // Arrow RIGHT: Phóng vù
                audio_play_cartoon_sfx(SFX_CT_RUN_AWAY);
                led_effects_color_burst(0, 200, 255);
                printf(" -> [MŨI TÊN PHẢI: PHÓNG VÈO BIẾN MẤT VÙÙÙ!]\r\n");
                return;
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

            // 7. Hàng chữ QWERTY: SIÊU ANH HÙNG & HÀNH ĐỘNG
            else if (upper == 'Q') { audio_play_cartoon_sfx(SFX_GAME_JUMP); led_effects_color_burst(50, 255, 100); printf(" -> [Q: MARIO NHẢY BỔNG BOING-UÝT!]\r\n"); }
            else if (upper == 'W') { audio_play_cartoon_sfx(SFX_GAME_SONIC_DASH); led_effects_color_burst(0, 150, 255); printf(" -> [W: SONIC SPIN-DASH SIÊU TỐC!]\r\n"); }
            else if (upper == 'E') { audio_play_cartoon_sfx(SFX_LASER); led_effects_color_burst(255, 30, 30); printf(" -> [E: SÚNG LASER BẮN TIA NĂNG LƯỢNG!]\r\n"); }
            else if (upper == 'R') { audio_play_cartoon_sfx(SFX_GAME_FIREBALL); led_effects_color_burst(255, 80, 0); printf(" -> [R: QUẢ CẦU LỬA NẢY TƯNG BẮN PIU CHÍU!]\r\n"); }
            else if (upper == 'T') { audio_play_cartoon_sfx(SFX_CT_TIMPANI_BOOM); led_effects_color_burst(255, 120, 0); printf(" -> [T: TRỐNG ĐỊNH ÂM TIMPANI BOOOOM!]\r\n"); }
            else if (upper == 'Y') { audio_play_cartoon_sfx(SFX_MARIO_COIN); led_effects_color_burst(255, 215, 0); printf(" -> [Y: NHẶT ĐỒNG XU VÀNG TING KENG!]\r\n"); }
            else if (upper == 'U') { audio_play_cartoon_sfx(SFX_GAME_POWERUP); led_effects_color_burst(255, 50, 255); printf(" -> [U: NÂNG CẤP NẤM BIẾN HÌNH POWER-UP!]\r\n"); }
            else if (upper == 'I') { audio_play_cartoon_sfx(SFX_GAME_1UP); led_effects_color_burst(0, 255, 200); printf(" -> [I: 1-UP HỒI MẠNG NHẠC TƯNG BỪNG!]\r\n"); }
            else if (upper == 'O') { audio_play_cartoon_sfx(SFX_GAME_BOMB); led_effects_color_burst(255, 255, 255); printf(" -> [O: BOM NỔ 8-BIT BÙM RẦM!]\r\n"); }
            else if (upper == 'P') { audio_play_cartoon_sfx(SFX_GAME_PUNCH); led_effects_color_burst(255, 0, 0); printf(" -> [P: CÚ ĐẤM CHÍ MẠNG BỐP BẸP!]\r\n"); }

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
            else if (upper == 'V') { audio_play_cartoon_sfx(SFX_FIRE_SIREN); printf(" -> [V: CÒI CỨU HỎA HOẠT HÌNH %s]\r\n", audio_cockpit_is_siren_on() ? "BẬT" : "TẮT"); }
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
            break;
        }

        // =====================================================
        // MODE 7: ĐIỆP VIÊN NHÍ & ROBOT R2-D2 (FULL TOÀN PHÍM)
        // =====================================================
        case MODE_7_SECRET_AGENT_ROBOT: {
            if (upper >= 'A' && upper <= 'Z') {
                audio_play_morse_char(c);
                led_effects_matrix();
                printf(" -> [ĐIỆP VIÊN: MÃ MORSE '%c' = %s (TÍT TE BÍP BÍP)]\r\n", upper, s_morse_letters[upper - 'A']);
            } else if (c >= '0' && c <= '9') {
                audio_play_morse_char(c);
                led_effects_matrix();
                printf(" -> [ĐIỆP VIÊN: MÃ MORSE SỐ '%c' = %s]\r\n", c, s_morse_digits[c - '0']);
            } else { // Phím mũi tên, Numpad, F-keys, Symbols...
                audio_play_robot_chirp();
                led_effects_matrix();
                printf(" -> [ROBOT R2-D2: LÍU LO BÍP BOOP CHIRP!]\r\n");
            }
            break;
        }

        // =====================================================
        // MODE 8: BUỒNG LÁI XE CỨU HỎA & 26 CON VẬT (FULL PHÍM)
        // =====================================================
        case MODE_8_COCKPIT_ANIMALS: {
            if (c == ' ') {
                audio_cockpit_engine_start();
                led_effects_color_burst(255, 100, 0);
                printf(" -> [BUỒNG LÁI: NỔ MÁY RỀN VANG VROOM VROOM!]\r\n");
            } else if (upper == 'S') {
                audio_cockpit_siren_toggle();
                printf(" -> [BUỒNG LÁI: CÒI CỨU HỎA ƯU TIÊN %s (ĐÈN XANH ĐỎ CHỚP)!]\r\n", audio_cockpit_is_siren_on() ? "BẬT" : "TẮT");
            } else if (upper == 'H') {
                audio_play_cartoon_sfx(SFX_CAR_HORN);
                led_effects_color_burst(255, 160, 0);
                printf(" -> [BUỒNG LÁI: BẤM CÒI XE BÍP BÍP!]\r\n");
            } else if (upper == 'B') {
                audio_play_cartoon_sfx(SFX_SKID_STOP);
                printf(" -> [BUỒNG LÁI: PHANH XE CHÁY ĐƯỜNG KÉÉÉT!]\r\n");
            } else if (c == '\r' || c == '\n' || kc == 0x58) {
                audio_cockpit_rocket_launch();
                led_effects_color_burst(255, 0, 0);
                printf(" -> [BUỒNG LÁI: TÊN LỬA 3... 2... 1... PHÓNG BLAST OFF!]\r\n");
            } else if (upper >= 'A' && upper <= 'Z') {
                audio_play_animal_sound(c);
                led_effects_color_burst(0, 255, 128);
                const char *animal = "CON VẬT";
                switch (upper) {
                    case 'A': animal = "CÁ SẤU ĐỚP MỒI!"; break;
                    case 'B': animal = "CHIM HÓT LÍU LO!"; break;
                    case 'C': animal = "MÈO KÊU MEO MEO!"; break;
                    case 'D': animal = "CHÓ SỦA GÂU GÂU!"; break;
                    case 'E': animal = "VOI RỐNG LOA TO!"; break;
                    case 'F': animal = "ẾCH KÊU ỘP ỘP!"; break;
                    case 'G': animal = "DÊ KÊU BE BE!"; break;
                    case 'H': animal = "NGỰA HÍ PHI NƯỚC ĐẠI!"; break;
                    case 'I': animal = "DẾ MÈN KÊU RI RI!"; break;
                    case 'J': animal = "BÁO ĐỐM GẦM!"; break;
                    case 'K': animal = "CHUỘT TÚI KANGAROO NHẢY!"; break;
                    case 'L': animal = "SƯ TỬ GẦM GRỪỪ!"; break;
                    case 'M': animal = "BÒ RỐNG ÙM BÒ!"; break;
                    case 'N': animal = "CHIM HỌA MI HÓT!"; break;
                    case 'O': animal = "CÚ MÈO GÙ GÙ!"; break;
                    case 'P': animal = "HEO KÊU ỤT ỊT!"; break;
                    case 'Q': animal = "VỊT KÊU CẠP CẠP!"; break;
                    case 'R': animal = "GÀ TRỐNG GÁY Ò Ó O!"; break;
                    case 'T': animal = "HỔ GẦM OAI PHONG!"; break;
                    case 'U': animal = "KỲ LÂN THẦN KỲ!"; break;
                    case 'V': animal = "CHIM ƯNG VỖ CÁNH!"; break;
                    case 'W': animal = "CHÓ SÓI HÚ TRU!"; break;
                    case 'X': animal = "CÁO RỪNG KÊU!"; break;
                    case 'Y': animal = "BÒ TÂY TẠNG ÙM BÒ!"; break;
                    case 'Z': animal = "NGỰA VẰN PHI LÓC CÓC!"; break;
                    default: animal = "TIẾNG CON VẬT VUI NHỘN!"; break;
                }
                printf(" -> [ĐẠI NHẠC HỘI: %s]\r\n", animal);
            } else if (kc >= 0x54 && kc <= 0x63) { // Numpad
                audio_play_cartoon_sfx(SFX_CAR_HORN);
                printf(" -> [BUỒNG LÁI: BẢNG ĐIỀU KHIỂN SỐ VŨ TRỤ!]\r\n");
            } else {
                audio_play_cartoon_sfx(SFX_LASER);
                printf(" -> [BUỒNG LÁI: BẬT NÚT ĐẶC CHỦNG!]\r\n");
            }
            break;
        }

        // =====================================================
        // MODE 9: BÉ HỌC ĐÁNH VẦN CHỮ CÁI & TẬP ĐẾM SỐ (PHONICS & COUNTING)
        // =====================================================
        case MODE_9_LEARN_PHONICS_NUMBERS: {
            if (upper >= 'A' && upper <= 'Z') {
                audio_play_phonics_letter(upper);
                // Unique bright color burst for each letter group
                uint8_t r = 50 + ((upper * 37) % 205);
                uint8_t g = 50 + ((upper * 67) % 205);
                uint8_t b = 50 + ((upper * 97) % 205);
                led_effects_color_burst(r, g, b);

                const char *spell_text = "CHỮ CÁI";
                switch (upper) {
                    case 'A': spell_text = "Chữ A -> A... Quả Táo / Con Gà!"; break;
                    case 'B': spell_text = "Chữ B (Bờ) -> Quả Bóng / Con Bò!"; break;
                    case 'C': spell_text = "Chữ C (Cờ) -> Con Cá / Cái Cốc!"; break;
                    case 'D': spell_text = "Chữ D (Dờ) -> Con Dê / Quả Dưa Hấu!"; break;
                    case 'E': spell_text = "Chữ E (E) -> Em Bé / Quả Me!"; break;
                    case 'F': spell_text = "Chữ F (Ép) -> Pháo Hoa Rực Rỡ!"; break;
                    case 'G': spell_text = "Chữ G (Gờ) -> Con Gà Trống / Cái Ghế!"; break;
                    case 'H': spell_text = "Chữ H (Hờ) -> Bông Hoa Đẹp / Con Hổ!"; break;
                    case 'I': spell_text = "Chữ I (I) -> Viên Bi Tròn / Trái Mận!"; break;
                    case 'J': spell_text = "Chữ J (Gi) -> Chiếc Hộp Nhạc Vui Nhộn!"; break;
                    case 'K': spell_text = "Chữ K (Ca) -> Cây Kem Mát Lạnh / Cái Kéo!"; break;
                    case 'L': spell_text = "Chữ L (Lờ) -> Chiếc Lá Xanh / Con Lợn Ỉn!"; break;
                    case 'M': spell_text = "Chữ M (Mờ) -> Mặt Trời Chiếu Sáng / Con Mèo Con!"; break;
                    case 'N': spell_text = "Chữ N (Nờ) -> Ngôi Sao Sáng / Quả Na!"; break;
                    case 'O': spell_text = "Chữ O (O) -> Quả Trứng Gà Tròn Xoe / Con Ong!"; break;
                    case 'P': spell_text = "Chữ P (Pờ) -> Cây Cầu Vồng / Đèn Pin Sáng!"; break;
                    case 'Q': spell_text = "Chữ Q (Quy) -> Con Quạ Đen / Cây Quạt Mát!"; break;
                    case 'R': spell_text = "Chữ R (Rờ) -> Con Rùa Chậm Chạp / Rừng Cây!"; break;
                    case 'S': spell_text = "Chữ S (Sờ) -> Chú Sóc Nâu Nhặt Hạt / Dòng Sông!"; break;
                    case 'T': spell_text = "Chữ T (Tờ) -> Đoàn Tàu Hỏa Xình Xịch / Con Tôm!"; break;
                    case 'U': spell_text = "Chữ U (U) -> Đám Mây Trắng / Cầu Vồng 7 Màu!"; break;
                    case 'V': spell_text = "Chữ V (Vờ) -> Chú Voi Con Vẫy Vòi / Con Vịt Vàng!"; break;
                    case 'W': spell_text = "Chữ W (Vê-Kép) -> Thế Giới Kỳ Thú / Bánh Xe!"; break;
                    case 'X': spell_text = "Chữ X (Xờ) -> Chiếc Xe Ô Tô Chạy Nhanh / Quả Xoài Ngọt!"; break;
                    case 'Y': spell_text = "Chữ Y (I-Dài) -> Yêu Thương Bố Mẹ / Cây Tre Xanh!"; break;
                    case 'Z': spell_text = "Chữ Z (Dét) -> Chú Ngựa Vằn Zebra / Đám Mây Sấm Sét!"; break;
                    default: spell_text = "Bé học đánh vần chữ cái vui nhộn!"; break;
                }
                printf(" -> [BÉ HỌC ĐÁNH VẦN: %s]\r\n", spell_text);
            } else if ((c >= '0' && c <= '9') || (kc >= 0x59 && kc <= 0x62)) {
                uint8_t num = 0;
                if (c >= '0' && c <= '9') num = c - '0';
                else if (kc >= 0x59 && kc <= 0x61) num = kc - 0x59 + 1; // Numpad 1..9
                else if (kc == 0x62) num = 0; // Numpad 0 = 0

                printf(" -> [BÉ HỌC SỐ: SỐ %d]\r\n", num);
                audio_play_counting_number(num);
            } else if (c != 0 && sb_play_symbol(c)) {
                led_effects_color_burst(100, 220, 255);
                printf(" -> [BÉ HỌC DẤU CÂU & KÝ TỰ: '%c']\r\n", c);
            } else if (sb_play_special_key(kc)) {
                led_effects_color_burst(255, 215, 0);
                printf(" -> [BÉ HỌC PHÍM CHỨC NĂNG & ĐIỀU HƯỚNG: 0x%02X]\r\n", kc);
            } else {
                audio_play_special_key(kc);
                led_effects_spark();
                printf(" -> [BÉ KHÁM PHÁ BÀN PHÍM: 0x%02X]\r\n", kc);
            }
            break;
        }

        default:
            break;
    }
}

// -------------------------------------------------------------
// HID KEY EVENT CALLBACK
// -------------------------------------------------------------
static void print_key_combo(uint8_t mod, uint8_t kc);

static void key_event_callback(key_event_t *key_event)
{
    if (key_event->state == KEY_STATE_RELEASED) {
        audio_piano_key_release(key_event->key_code);
        return;
    }
    if (KEY_STATE_PRESSED != key_event->state) {
        return;
    }

    uint8_t kc = key_event->key_code;
    uint8_t mod = key_event->modifier;
    bool is_shift = hid_keyboard_is_modifier_shift(mod);

    // Xử lý tổ hợp Ctrl / Alt / Win
    bool has_ctrl = (mod & 0x11) != 0;
    bool has_alt  = (mod & 0x44) != 0;
    bool has_gui  = (mod & 0x88) != 0;

    if (has_ctrl || has_alt || has_gui) {
        print_key_combo(mod, kc);
        handle_toy_mode_key(kc, mod, 0); // Vẫn kích hoạt âm thanh cho phím tổ hợp!
        return;
    }

    char c = 0;

    // 1. Letters: 'a' - 'z' (0x04 - 0x1D)
    if (kc >= 0x04 && kc <= 0x1D) {
        bool upper = is_shift ^ s_caps_lock;
        c = upper ? ('A' + (kc - 0x04)) : ('a' + (kc - 0x04));
        putchar(c);
        handle_toy_mode_key(kc, mod, c);
        return;
    }

    // 2. Special control keys (Esc, Tab)
    if (kc == 0x29) { // Escape
        printf("[ESC]");
        handle_toy_mode_key(kc, mod, 0);
        return;
    }
    if (kc == 0x2B) { // Tab
        printf("[TAB]");
        handle_toy_mode_key(kc, mod, '\t');
        return;
    }

    // 3. Numbers & symbols (0x1E - 0x38)
    if (kc >= 0x1E && kc <= 0x38) {
        uint8_t shift_idx = is_shift ? 1 : 0;
        c = keycode2ascii[kc][shift_idx];
        if (c == KEYBOARD_ENTER_MAIN_CHAR) {
            printf("[ENTER]");
        } else if (c == '\b') {
            printf("[BACKSPACE]");
        } else if (c) {
            putchar(c);
        }
        handle_toy_mode_key(kc, mod, c);
        return;
    }

    if (kc == 0x39) { // CapsLock
        s_caps_lock = !s_caps_lock;
        trigger_led_update();
        printf("[CAPSLOCK: %s]", s_caps_lock ? "BẬT" : "TẮT");
        handle_toy_mode_key(kc, mod, 0);
        return;
    }

    // 4. Function keys (F1 - F12)
    if (kc >= 0x3A && kc <= 0x45) {
        printf("[F%d]", (kc - 0x3A + 1));
        handle_toy_mode_key(kc, mod, 0);
        return;
    }

    // 5. Navigation & Editing keys
    switch (kc) {
        case 0x46: printf("[PRTSC]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x47: 
            s_scroll_lock = !s_scroll_lock;
            trigger_led_update();
            printf("[SCRLK: %s]", s_scroll_lock ? "BẬT" : "TẮT"); 
            handle_toy_mode_key(kc, mod, 0);
            return;
        case 0x48: printf("[PAUSE]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x49: printf("[INS]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x4A: printf("[HOME]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x4B: printf("[PGUP]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x4C: printf("[DEL]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x4D: printf("[END]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x4E: printf("[PGDN]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x4F: printf("[RIGHT]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x50: printf("[LEFT]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x51: printf("[DOWN]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x52: printf("[UP]"); handle_toy_mode_key(kc, mod, 0); return;
        case 0x53: // NumLock
            s_num_lock = !s_num_lock;
            trigger_led_update();
            printf("[NUMLOCK: %s]", s_num_lock ? "BẬT" : "TẮT");
            handle_toy_mode_key(kc, mod, 0);
            return;
        default:
            break;
    }

    // 6. Numpad keys (0x54 - 0x63)
    if (kc >= 0x54 && kc <= 0x63) {
        switch (kc) {
            case 0x54: c = '/'; printf("[NUM /]"); break;
            case 0x55: c = '*'; printf("[NUM *]"); break;
            case 0x56: c = '-'; printf("[NUM -]"); break;
            case 0x57: c = '+'; printf("[NUM +]"); break;
            case 0x58: c = '\r'; printf("[NUM ENTER]"); break;
            case 0x59: c = '1'; printf("[NUM 1]"); break;
            case 0x5A: c = '2'; printf("[NUM 2]"); break;
            case 0x5B: c = '3'; printf("[NUM 3]"); break;
            case 0x5C: c = '4'; printf("[NUM 4]"); break;
            case 0x5D: c = '5'; printf("[NUM 5]"); break;
            case 0x5E: c = '6'; printf("[NUM 6]"); break;
            case 0x5F: c = '7'; printf("[NUM 7]"); break;
            case 0x60: c = '8'; printf("[NUM 8]"); break;
            case 0x61: c = '9'; printf("[NUM 9]"); break;
            case 0x62: c = '0'; printf("[NUM 0]"); break;
            case 0x63: c = '.'; printf("[NUM .]"); break;
        }
        handle_toy_mode_key(kc, mod, c);
        return;
    }

    // Any other keycode
    handle_toy_mode_key(kc, mod, 0);
}

static void print_key_combo(uint8_t mod, uint8_t kc)
{
    printf("[");
    bool first = true;
    if (mod & 0x11) { printf("CTRL"); first = false; }
    if (mod & 0x44) { if (!first) printf("+"); printf("ALT"); first = false; }
    if (mod & 0x88) { if (!first) printf("+"); printf("WIN"); first = false; }
    if (mod & 0x22) { if (!first) printf("+"); printf("SHIFT"); first = false; }

    if (kc >= 0x04 && kc <= 0x1D) {
        if (!first) printf("+");
        printf("%c", 'A' + (kc - 0x04));
    } else if (kc >= 0x1E && kc <= 0x27) {
        if (!first) printf("+");
        printf("%c", (kc == 0x27) ? '0' : ('1' + (kc - 0x1E)));
    } else if (kc >= 0x3A && kc <= 0x45) {
        if (!first) printf("+");
        printf("F%d", (kc - 0x3A + 1));
    } else if (kc == 0x28) {
        if (!first) printf("+");
        printf("ENTER");
    } else if (kc == 0x29) {
        if (!first) printf("+");
        printf("ESC");
    } else if (kc == 0x2C) {
        if (!first) printf("+");
        printf("SPACE");
    }
    printf("]");
}

static inline bool key_found(const uint8_t *const src, uint8_t key, unsigned int length)
{
    for (unsigned int i = 0; i < length; i++) {
        if (src[i] == key) return true;
    }
    return false;
}

static void hid_host_keyboard_report_callback(const uint8_t *const data, const int length)
{
    hid_keyboard_input_report_boot_t *kb_report = (hid_keyboard_input_report_boot_t *)data;
    if (length < sizeof(hid_keyboard_input_report_boot_t)) return;

    static uint8_t prev_keys[HID_KEYBOARD_KEY_MAX] = { 0 };
    static uint8_t prev_modifier = 0;
    key_event_t key_event;

    uint8_t curr_modifier = kb_report->modifier.val;
    uint8_t pressed_mods = curr_modifier & (~prev_modifier);
    bool any_key_pressed = false;

    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        if (kb_report->key[i] > HID_KEY_ERROR_UNDEFINED) {
            any_key_pressed = true;
            break;
        }
    }

    // Nhận diện phím Modifier bấm đơn lẻ (Ctrl, Alt, Win, Shift)
    if (!any_key_pressed && pressed_mods != 0) {
        if (pressed_mods & 0x01) { printf("[LCTRL]"); handle_toy_mode_key(0xE0, 0x01, 0); }
        if (pressed_mods & 0x04) { printf("[LALT]"); handle_toy_mode_key(0xE2, 0x04, 0); }
        if (pressed_mods & 0x08) { printf("[LWIN]"); handle_toy_mode_key(0xE3, 0x08, 0); }
        if (pressed_mods & 0x10) { printf("[RCTRL]"); handle_toy_mode_key(0xE4, 0x10, 0); }
        if (pressed_mods & 0x40) { printf("[RALT]"); handle_toy_mode_key(0xE6, 0x40, 0); }
        if (pressed_mods & 0x80) { printf("[RWIN]"); handle_toy_mode_key(0xE7, 0x80, 0); }
        if (pressed_mods & 0x02) { printf("[LSHIFT]"); handle_toy_mode_key(0xE1, 0x02, 0); }
        if (pressed_mods & 0x20) { printf("[RSHIFT]"); handle_toy_mode_key(0xE5, 0x20, 0); }
    }
    prev_modifier = curr_modifier;

    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        if (prev_keys[i] > HID_KEY_ERROR_UNDEFINED &&
            !key_found(kb_report->key, prev_keys[i], HID_KEYBOARD_KEY_MAX)) {
            key_event.key_code = prev_keys[i];
            key_event.modifier = 0;
            key_event.state = KEY_STATE_RELEASED;
            key_event_callback(&key_event);
        }

        if (kb_report->key[i] > HID_KEY_ERROR_UNDEFINED &&
            !key_found(prev_keys, kb_report->key[i], HID_KEYBOARD_KEY_MAX)) {
            key_event.key_code = kb_report->key[i];
            key_event.modifier = kb_report->modifier.val;
            key_event.state = KEY_STATE_PRESSED;
            key_event_callback(&key_event);
        }
    }

    memcpy(prev_keys, &kb_report->key, HID_KEYBOARD_KEY_MAX);
}

static void hid_host_mouse_report_callback(const uint8_t *const data, const int length) {
    // Mouse support stub
}

static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                        const hid_host_interface_event_t event,
                                        void *arg)
{
    uint8_t data[64] = { 0 };
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle,
                                                                  data,
                                                                  64,
                                                                  &data_length));
        if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
            if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
                hid_host_keyboard_report_callback(data, data_length);
            } else if (HID_PROTOCOL_MOUSE == dev_params.proto) {
                hid_host_mouse_report_callback(data, data_length);
            }
        }
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HID Device disconnected");
        if (s_kb_handle == hid_device_handle) {
            s_kb_handle = NULL;
        }
        ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "HID Device transfer error");
        break;
    default:
        break;
    }
}

static void hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                                  const hid_host_driver_event_t event,
                                  void *arg)
{
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
        ESP_LOGI(TAG, "HID Device connected: Proto: %s", hid_proto_name_str[dev_params.proto]);

        const hid_host_device_config_t dev_config = {
            .callback = hid_host_interface_callback,
            .callback_arg = NULL
        };

        ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));

        if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
            ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT));
            if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
                ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
                s_kb_handle = hid_device_handle;
                trigger_led_update(); // Sync NumLock LED on connect
            }
        }
        ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
        break;
    default:
        break;
    }
}

static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                     const hid_host_driver_event_t event,
                                     void *arg)
{
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        app_event_queue_t evt_queue = {
            .event_group = APP_EVENT_HID_HOST,
            .hid_host_device.handle = hid_device_handle,
            .hid_host_device.event = event,
            .hid_host_device.arg = arg
        };
        xQueueSend(app_event_queue, &evt_queue, 0);
    }
}

static void usb_lib_task(void *pvParameters)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));
    xTaskNotifyGive(pvParameters);

    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
}

// Background task: Speed runner idle monitor & Cockpit police siren
static void toy_background_task(void *pvParameters) {
    bool police_toggle = false;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));

        if (s_current_mode == MODE_2_BABY_MOZART) {
            audio_mozart_autoplay_tick();
        }

        if (s_current_mode == MODE_3_SPEED_RUNNER) {
            audio_speed_runner_idle_check();
        }

        if (s_current_mode == MODE_8_COCKPIT_ANIMALS && audio_cockpit_is_siren_on()) {
            police_toggle = !police_toggle;
            led_effects_police(police_toggle);
        }
    }
}

static void on_encoder_rotated(int delta) {
    if (app_event_queue) {
        app_event_queue_t evt = {
            .event_group = APP_EVENT_ENCODER_ROTATE,
            .encoder_delta = delta
        };
        xQueueSend(app_event_queue, &evt, 0);
    }
}

static void on_encoder_clicked(void) {
    if (app_event_queue) {
        app_event_queue_t evt = {
            .event_group = APP_EVENT_ENCODER_CLICK
        };
        xQueueSend(app_event_queue, &evt, 0);
    }
}

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "   ESP32-S3 N16R8: SMART SENSORY KEYBOARD TOY STARTING    ");
    ESP_LOGI(TAG, "==========================================================");

    // Create system event queue first
    app_event_queue = xQueueCreate(32, sizeof(app_event_queue_t));

    // 1. Report PSRAM (8MB Octal) and Flash (16MB)
    size_t psram_size = esp_psram_get_size();
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_free = esp_get_free_heap_size();
    ESP_LOGI(TAG, ">>> [N16R8 HARDWARE READY]");
    ESP_LOGI(TAG, ">>> PSRAM Total: %u MB (8MB Octal OPI)", (unsigned int)(psram_size / (1024 * 1024)));
    ESP_LOGI(TAG, ">>> PSRAM Free : %u KB", (unsigned int)(psram_free / 1024));
    ESP_LOGI(TAG, ">>> Total Free Heap: %u KB", (unsigned int)(total_free / 1024));

    // 2. Initialize Audio Synthesizer Engine (Core 1)
    ESP_ERROR_CHECK(audio_synth_init());

    // 2.1 Initialize Soundbank (LittleFS + PSRAM Dynamic Sampler)
    soundbank_init();

    // 3. Initialize WS2812B RGB LED Strip (GPIO 4 via RMT)
    ESP_ERROR_CHECK(led_effects_init());

    // 4. Initialize HW-040 Rotary Encoder (CLK=14, DT=13, SW=12)
    ESP_ERROR_CHECK(rotary_encoder_init(on_encoder_rotated, on_encoder_clicked));

    // 5. Start toy background monitor task
    xTaskCreate(toy_background_task, "toy_bg", 3072, NULL, 4, NULL);

    // 6. Announce Mode 1
    switch_toy_mode(MODE_1_PIANO);

    // 7. Start USB Host Library & HID Driver (Core 0)
    BaseType_t task_created = xTaskCreatePinnedToCore(usb_lib_task,
                                                      "usb_events",
                                                      4096,
                                                      xTaskGetCurrentTaskHandle(),
                                                      10, NULL, 0);
    assert(task_created == pdTRUE);
    ulTaskNotifyTake(false, 1000);

    const hid_host_driver_config_t hid_host_driver_config = {
        .create_background_task = true,
        .task_priority = 10,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_callback,
        .callback_arg = NULL
    };

    ESP_ERROR_CHECK(hid_host_install(&hid_host_driver_config));

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "USB HOST READY! Hay cam ban phim USB vao cong Type-C OTG");
    ESP_LOGI(TAG, "==========================================================");

    app_event_queue_t evt_queue;
    while (1) {
        if (xQueueReceive(app_event_queue, &evt_queue, portMAX_DELAY)) {
            if (APP_EVENT_HID_HOST == evt_queue.event_group) {
                hid_host_device_event(evt_queue.hid_host_device.handle,
                                      evt_queue.hid_host_device.event,
                                      evt_queue.hid_host_device.arg);
            } else if (APP_EVENT_UPDATE_LEDS == evt_queue.event_group) {
                update_keyboard_leds();
            } else if (APP_EVENT_ENCODER_ROTATE == evt_queue.event_group) {
                if (evt_queue.encoder_delta > 0) {
                    audio_synth_volume_up();
                    printf("\r\n>>> [NÚM XOAY HW-040: TĂNG ÂM LƯỢNG: %d%%]\r\n", (int)(audio_synth_get_volume() * 100));
                    audio_play_volume_tick(audio_synth_get_volume(), true);
                } else if (evt_queue.encoder_delta < 0) {
                    audio_synth_volume_down();
                    printf("\r\n>>> [NÚM XOAY HW-040: GIẢM ÂM LƯỢNG: %d%%]\r\n", (int)(audio_synth_get_volume() * 100));
                    audio_play_volume_tick(audio_synth_get_volume(), false);
                }
                fflush(stdout);
                led_effects_volume(audio_synth_get_volume());
            } else if (APP_EVENT_ENCODER_CLICK == evt_queue.event_group) {
                printf("\r\n>>> [NÚM XOAY HW-040: NHẤN SW -> CHUYỂN CHẾ ĐỘ TIẾP THEO]\r\n");
                fflush(stdout);
                switch_toy_mode((toy_mode_t)((s_current_mode + 1) % MODE_MAX_COUNT));
            }
        }
    }
}
