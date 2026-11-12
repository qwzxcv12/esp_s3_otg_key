/*
 * SPDX-FileCopyrightText: 2022-2026 Espressif Systems (Shanghai) CO LTD
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
#include "button_driver.h"
#include "soundbank.h"
#include "mode_manager.h"
#include "power_manager.h"
// #include "pn532_driver.h"  // TẠM TẮT - module PN532 chưa kết nối được
#include "msc_host.h"
#include "msc_host_vfs.h"
#include "esp_vfs_fat.h"
#include "mp3_player.h"
#include "wifi_ota.h"

static const char *TAG = "usb_toy";

QueueHandle_t app_event_queue = NULL;

typedef enum {
    APP_EVENT_HID_HOST = 0,
    APP_EVENT_UPDATE_LEDS,
    APP_EVENT_BUTTON_PRESS,
    APP_EVENT_MSC_CONNECTED,
    APP_EVENT_MSC_DISCONNECTED
} app_event_group_t;

typedef struct {
    app_event_group_t event_group;
    union {
        struct {
            hid_host_device_handle_t handle;
            hid_host_driver_event_t event;
            void *arg;
        } hid_host_device;
        struct {
            button_id_t id;
            button_press_type_t type;
        } button;
        struct {
            uint8_t address;
        } msc_dev;
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

// -------------------------------------------------------------
// POWER MANAGEMENT CALLBACKS (ÂM TẠM BIỆT & ĐÁNH THỨC)
// -------------------------------------------------------------
static void on_pm_pre_sleep(void) {
    printf("\r\n>>> [HỆ THỐNG CHUẨN BỊ NGỦ: PHÁT ÂM THANH TẠM BIỆT] <<<\r\n");
    audio_play_goodbye_lullaby();
}

static void on_pm_enter_sleep(void) {
    printf("\r\n>>> [HỆ THỐNG VÀO CHẾ ĐỘ NGỦ: TẮT NGOẠI VI & ĐÈN BÀN PHÍM] <<<\r\n");
    // 1. Tắt toàn bộ đèn LED trên bàn phím USB (NumLock, CapsLock, ScrollLock)
    if (s_kb_handle != NULL) {
        uint8_t led_report = 0;
        hid_class_request_set_report(s_kb_handle, HID_REPORT_TYPE_OUTPUT, 0, &led_report, 1);
    }
    // 2. Tắt clock I2S MAX98357A đưa về Hardware Shutdown (< 10uA)
    audio_synth_enter_low_power();
    // 3. Tạm dừng quét thẻ RFID/NFC PN532 để tiết kiệm điện
    // pn532_set_scanner_active(false);  // TẠM TẮT
}

static void on_pm_wake_up(void) {
    printf("\r\n>>> [HỆ THỐNG ĐÃ ĐƯỢC ĐÁNH THỨC: KHÔI PHỤC HOẠT ĐỘNG] <<<\r\n");
    // 1. Mở lại clock I2S MAX98357A
    audio_synth_exit_low_power();
    // 2. Kích hoạt lại tiến trình quét thẻ PN532
    // pn532_set_scanner_active(true);  // TẠM TẮT
    // 2. Khôi phục đèn NumLock trên bàn phím USB
    s_num_lock = true;
    trigger_led_update();
    // 3. Phát âm thanh Chime đánh thức vui tươi
    audio_play_wake_chime();
}

static bool on_pm_can_sleep(void) {
    // 1. Chế độ nghe nhạc (Mode 10: Máy phát nhạc USB MP3 / WAV, chỉ mục 9): Không tự động ngủ
    if (mode_manager_get_current_index() == 9) {
        return false;
    }
    // 2. Kiểm tra nếu trình phát nhạc MP3 đang phát
    if (mp3_player_get_state() == MP3_STATE_PLAYING) {
        return false;
    }
    // 3. Kiểm tra nếu Baby Mozart đang tự động biểu diễn (autoplay)
    if (audio_mozart_is_autoplay()) {
        return false;
    }
    return true;
}

// -------------------------------------------------------------
// TOY KEY EVENT HANDLER (GLOBAL SHORTCUTS & MODE DISPATCHER)
// -------------------------------------------------------------
static int64_t s_last_mode_switch_us = 0;
static int64_t s_last_esc_press_us = 0;

static void handle_toy_key(uint8_t kc, uint8_t mod, char c) {
    int64_t now_us = esp_timer_get_time();
    bool is_mode_9 = (mode_manager_get_current_index() == 8);
    bool has_ctrl_or_alt = ((mod & 0x55) != 0);

    // 1. Phím tắt hệ thống toàn cục (Global System Shortcuts)
    if (kc == 0x44) { // F11: Giảm âm lượng
        audio_synth_volume_down();
        printf(" -> [GIẢM ÂM LƯỢNG: %d%%]\r\n", (int)(audio_synth_get_volume() * 100));
        audio_play_volume_tick(audio_synth_get_volume(), false);
        led_effects_volume(audio_synth_get_volume());
        return;
    }
    if (kc == 0x45) { // F12: Tăng âm lượng
        audio_synth_volume_up();
        printf(" -> [TĂNG ÂM LƯỢNG: %d%%]\r\n", (int)(audio_synth_get_volume() * 100));
        audio_play_volume_tick(audio_synth_get_volume(), true);
        led_effects_volume(audio_synth_get_volume());
        return;
    }

    if (kc == 0x29) { // ESC
        if (mode_manager_is_in_standby()) {
            mode_manager_switch_to(0);
            return;
        }
        if (is_mode_9 && !has_ctrl_or_alt) {
            // Trong Mode 9 (Bé Học Đánh Vần), bấm ESC đơn lẻ đọc "Phím Escape",
            // bấm đúp ESC trong vòng 650ms sẽ chuyển chế độ ngẫu nhiên!
            if ((now_us - s_last_esc_press_us) < 650000LL) {
                s_last_esc_press_us = 0;
                s_last_mode_switch_us = now_us;
                mode_manager_random();
                return;
            }
            s_last_esc_press_us = now_us;
            // Chuyển xuống Mode 9 để đọc "Phím Escape"
        } else {
            if ((now_us - s_last_mode_switch_us) > 400000LL) {
                s_last_mode_switch_us = now_us;
                mode_manager_random();
            }
            return;
        }
    }

    if (kc >= 0x3A && kc <= 0x43) { // F1 - F10 (kc 0x3A = F1 ... 0x43 = F10)
        uint8_t target_mode = kc - 0x3A;
        if (target_mode == 9 && !mp3_player_is_usb_mounted()) {
            printf("\r\n>>> [PHÍM F10]: Chưa cắm USB Flash Drive chứa nhạc! Hãy cắm USB vào cổng Type-C <<<\r\n");
            led_effects_color_burst(255, 120, 0);
            return;
        }
        if (is_mode_9 && !has_ctrl_or_alt) {
            // Trong Mode 9, nhấn F1..F10 đơn lẻ cho phép bé học tên phím
        } else {
            if ((now_us - s_last_mode_switch_us) > 400000LL) {
                s_last_mode_switch_us = now_us;
                mode_manager_switch_to(target_mode);
            }
            return;
        }
    }

    // 2. Chuyển tiếp sự kiện tới chế độ hiện tại qua Mode Manager
    mode_manager_dispatch_key(kc, mod, c);
}

// -------------------------------------------------------------
// HID KEY EVENT CALLBACK
// -------------------------------------------------------------
static void print_key_combo(uint8_t mod, uint8_t kc);

static void key_event_callback(key_event_t *key_event)
{
    if (key_event->state == KEY_STATE_RELEASED) {
        mode_manager_dispatch_release(key_event->key_code);
        return;
    }
    if (KEY_STATE_PRESSED != key_event->state) {
        return;
    }

    // Đánh thức và nuôi bộ đếm rảnh khi có bất kỳ phím nào được nhấn
    power_manager_feed_activity();

    uint8_t kc = key_event->key_code;
    uint8_t mod = key_event->modifier;
    bool is_shift = hid_keyboard_is_modifier_shift(mod);

    bool has_ctrl = (mod & 0x11) != 0;
    bool has_alt  = (mod & 0x44) != 0;
    bool has_gui  = (mod & 0x88) != 0;
    bool is_combo = (has_ctrl || has_alt || has_gui);

    if (is_combo) {
        print_key_combo(mod, kc);
    }

    char c = 0;

    // 1. Chữ cái: 'a' - 'z' (0x04 - 0x1D)
    if (kc >= 0x04 && kc <= 0x1D) {
        bool upper = is_shift ^ s_caps_lock;
        c = upper ? ('A' + (kc - 0x04)) : ('a' + (kc - 0x04));
        if (!is_combo) putchar(c);
        handle_toy_key(kc, mod, c);
        return;
    }

    // 2. Phím điều khiển đặc biệt (Esc, Tab)
    if (kc == 0x29) { // Escape
        if (!is_combo) printf("[ESC]");
        handle_toy_key(kc, mod, 0);
        return;
    }
    if (kc == 0x2B) { // Tab
        if (!is_combo) printf("[TAB]");
        handle_toy_key(kc, mod, '\t');
        return;
    }

    // 3. Phím số & ký hiệu chính (0x1E - 0x38)
    if (kc >= 0x1E && kc <= 0x38) {
        uint8_t shift_idx = is_shift ? 1 : 0;
        c = keycode2ascii[kc][shift_idx];
        if (!is_combo) {
            if (c == KEYBOARD_ENTER_MAIN_CHAR) {
                printf("[ENTER]");
            } else if (c == '\b') {
                printf("[BACKSPACE]");
            } else if (c) {
                putchar(c);
            }
        }
        handle_toy_key(kc, mod, c);
        return;
    }

    if (kc == 0x39) { // CapsLock
        s_caps_lock = !s_caps_lock;
        trigger_led_update();
        printf("[CAPSLOCK: %s]", s_caps_lock ? "BẬT" : "TẮT");
        handle_toy_key(kc, mod, 0);
        return;
    }

    // 4. Các phím chức năng (F1 - F12)
    if (kc >= 0x3A && kc <= 0x45) {
        if (!is_combo) printf("[F%d]", (kc - 0x3A + 1));
        handle_toy_key(kc, mod, 0);
        return;
    }

    // 5. Cụm phím điều hướng & soạn thảo
    switch (kc) {
        case 0x46: printf("[PRTSC]"); handle_toy_key(kc, mod, 0); return;
        case 0x47: 
            s_scroll_lock = !s_scroll_lock;
            trigger_led_update();
            printf("[SCRLK: %s]", s_scroll_lock ? "BẬT" : "TẮT"); 
            handle_toy_key(kc, mod, 0);
            return;
        case 0x48: printf("[PAUSE]"); handle_toy_key(kc, mod, 0); return;
        case 0x49: printf("[INS]"); handle_toy_key(kc, mod, 0); return;
        case 0x4A: printf("[HOME]"); handle_toy_key(kc, mod, 0); return;
        case 0x4B: printf("[PGUP]"); handle_toy_key(kc, mod, 0); return;
        case 0x4C: printf("[DEL]"); handle_toy_key(kc, mod, 0); return;
        case 0x4D: printf("[END]"); handle_toy_key(kc, mod, 0); return;
        case 0x4E: printf("[PGDN]"); handle_toy_key(kc, mod, 0); return;
        case 0x4F: printf("[RIGHT]"); handle_toy_key(kc, mod, 0); return;
        case 0x50: printf("[LEFT]"); handle_toy_key(kc, mod, 0); return;
        case 0x51: printf("[DOWN]"); handle_toy_key(kc, mod, 0); return;
        case 0x52: printf("[UP]"); handle_toy_key(kc, mod, 0); return;
        case 0x53: // NumLock
            s_num_lock = !s_num_lock;
            trigger_led_update();
            printf("[NUMLOCK: %s]", s_num_lock ? "BẬT" : "TẮT");
            handle_toy_key(kc, mod, 0);
            return;
        default:
            break;
    }

    // 6. Cụm bàn phím số Numpad (0x54 - 0x63)
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
        handle_toy_key(kc, mod, c);
        return;
    }

    // Tất cả các phím khác
    handle_toy_key(kc, mod, 0);
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
    static int64_t s_last_slap_us = 0;
    key_event_t key_event;

    // 1. Phát hiện bé đập cả bàn tay lên bàn phím (6-Key Rollover / Phantom State = 0x01)
    if (kb_report->key[0] == HID_KEY_ROLLOVER) {
        power_manager_feed_activity();
        int64_t now_us = esp_timer_get_time();
        if ((now_us - s_last_slap_us) > 350000LL) {
            s_last_slap_us = now_us;
            printf("\r\n[PALM SLAP! -> BÙNG NỔ HỢP ÂM VUI NHỘN!]\r\n");
            audio_play_slap_chord();
            led_effects_color_burst(255, 220, 80);
        }
        return;
    }

    uint8_t curr_modifier = kb_report->modifier.val;
    uint8_t pressed_mods = curr_modifier & (~prev_modifier);
    uint8_t released_mods = prev_modifier & (~curr_modifier);
    bool any_key_pressed = false;

    if (pressed_mods != 0) {
        power_manager_feed_activity();
    }

    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        if (kb_report->key[i] > HID_KEY_ERROR_UNDEFINED) {
            any_key_pressed = true;
            break;
        }
    }

    // 2. Xử lý nhả phím Modifier (0xE0..0xE7) để không bị treo âm thanh trên các phím này
    if (released_mods != 0) {
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (released_mods & (1 << bit)) {
                mode_manager_dispatch_release(0xE0 + bit);
            }
        }
    }

    // 3. Nhận diện phím Modifier bấm đơn lẻ (Ctrl, Alt, Win, Shift)
    if (!any_key_pressed && pressed_mods != 0) {
        if (pressed_mods & 0x01) { printf("[LCTRL]"); handle_toy_key(0xE0, 0x01, 0); }
        if (pressed_mods & 0x04) { printf("[LALT]"); handle_toy_key(0xE2, 0x04, 0); }
        if (pressed_mods & 0x08) { printf("[LWIN]"); handle_toy_key(0xE3, 0x08, 0); }
        if (pressed_mods & 0x10) { printf("[RCTRL]"); handle_toy_key(0xE4, 0x10, 0); }
        if (pressed_mods & 0x40) { printf("[RALT]"); handle_toy_key(0xE6, 0x40, 0); }
        if (pressed_mods & 0x80) { printf("[RWIN]"); handle_toy_key(0xE7, 0x80, 0); }
        if (pressed_mods & 0x02) { printf("[LSHIFT]"); handle_toy_key(0xE1, 0x02, 0); }
        if (pressed_mods & 0x20) { printf("[RSHIFT]"); handle_toy_key(0xE5, 0x20, 0); }
    }
    prev_modifier = curr_modifier;

    int newly_pressed_count = 0;
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
            newly_pressed_count++;
            key_event.key_code = kb_report->key[i];
            key_event.modifier = kb_report->modifier.val;
            key_event.state = KEY_STATE_PRESSED;
            key_event_callback(&key_event);
        }
    }

    // Nếu bé đập từ 4 phím trở lên cùng một tích tắc (1 USB frame), thưởng thêm hiệu ứng pháo hoa LED!
    if (newly_pressed_count >= 4) {
        int64_t now_us = esp_timer_get_time();
        if ((now_us - s_last_slap_us) > 350000LL) {
            s_last_slap_us = now_us;
            led_effects_color_burst(255, 220, 80);
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

static msc_host_device_handle_t s_msc_device_handle = NULL;
static msc_host_vfs_handle_t s_msc_vfs_handle = NULL;
static uint8_t s_prev_mode_before_usb = 0;

static void msc_host_event_callback(const msc_host_event_t *event, void *arg)
{
    if (event->event == MSC_DEVICE_CONNECTED) {
        ESP_LOGI(TAG, "USB MSC Device connected (address %d)", event->device.address);
        app_event_queue_t evt_queue = {
            .event_group = APP_EVENT_MSC_CONNECTED,
            .msc_dev = {
                .address = event->device.address
            }
        };
        xQueueSend(app_event_queue, &evt_queue, 0);
    } else if (event->event == MSC_DEVICE_DISCONNECTED) {
        ESP_LOGI(TAG, "USB MSC Device disconnected");
        app_event_queue_t evt_queue = {
            .event_group = APP_EVENT_MSC_DISCONNECTED
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

// Background task: Cập nhật LED non-blocking mỗi 25ms và gọi tác vụ nền Mode Manager mỗi 50ms
static void toy_background_task(void *pvParameters) {
    uint8_t divider = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(25));
        power_manager_tick(25);
        led_effects_tick(25);
        divider ^= 1;
        if (divider == 0 && !power_manager_is_sleeping()) {
            mode_manager_tick(50);
        }
    }
}

static void on_button_event(button_id_t btn, button_press_type_t type) {
    if (app_event_queue) {
        app_event_queue_t evt = {
            .event_group = APP_EVENT_BUTTON_PRESS,
            .button = {
                .id = btn,
                .type = type
            }
        };
        xQueueSend(app_event_queue, &evt, 0);
    }
}

// -------------------------------------------------------------
// BẢNG ÁNH XẠ THẺ PHÉP THUẬT NFC / RFID (MAGIC CARDS)
// Bạn có thể quẹt thẻ thực tế để lấy UID in ra màn hình Terminal,
// sau đó thêm mã UID vào bảng này để tự động chuyển Mode hoặc đổi nhạc!
// -------------------------------------------------------------
typedef struct {
    const char *uid_hex;       // Chuỗi UID viết hoa liền nhau (ví dụ "04A1B2C3" hoặc "4A67B2C1")
    uint8_t target_mode;       // Chế độ đích (1..10), 0 là không đổi chế độ
    const char *card_name;     // Tên thẻ thân thiện
} magic_card_entry_t;

static const magic_card_entry_t s_magic_cards[] = {
    { "04A1B2C3", 1, "Thẻ Đàn Piano Diệu Kỳ (Mode 1)" },
    { "04A2B2C3", 2, "Thẻ Nghệ Sĩ Nhí Mozart (Mode 2)" },
    { "04A3B2C3", 4, "Thẻ Dàn Trống Beat-Maker & DJ (Mode 4)" },
    { "04A4B2C3", 8, "Thẻ Buồng Lái Xe Cứu Hỏa & 26 Con Thú (Mode 8)" },
    { "04A5B2C3", 9, "Thẻ Bé Học Đánh Vần & Tập Đếm (Mode 9)" },
    { "04A6B2C3", 10, "Thẻ Máy Hát Thiếu Nhi MP3 (Mode 10)" },
};

static void on_rfid_card_event(const uint8_t *uid, uint8_t uid_len) {
    power_manager_feed_activity();

    char uid_compact[32] = {0};
    char uid_spaced[48] = {0};
    for (int i = 0; i < uid_len && i < 10; i++) {
        sprintf(uid_compact + strlen(uid_compact), "%02X", uid[i]);
        sprintf(uid_spaced + strlen(uid_spaced), "%02X ", uid[i]);
    }

    printf("\r\n======================================================\r\n");
    printf(">>> [NFC / RFID THẺ THẦN KỲ]: ĐÃ PHÁT HIỆN THẺ MỚI! <<<\r\n");
    printf(">>> UID (%d bytes): [ %s]\r\n", uid_len, uid_spaced);

    // Kiểm tra xem thẻ đã được gán hành động trong bảng s_magic_cards chưa
    bool matched = false;
    for (int i = 0; i < sizeof(s_magic_cards) / sizeof(s_magic_cards[0]); i++) {
        if (strcasecmp(uid_compact, s_magic_cards[i].uid_hex) == 0) {
            matched = true;
            printf(">>> [TÊN THẺ]: %s <<<\r\n", s_magic_cards[i].card_name);
            printf(">>> [HÀNH ĐỘNG]: Chuyển sang Mode %d ngay tức thì!\r\n", s_magic_cards[i].target_mode);
            printf("======================================================\r\n\n");

            if (s_magic_cards[i].target_mode >= 1 && s_magic_cards[i].target_mode <= 10) {
                uint8_t mode_idx = s_magic_cards[i].target_mode - 1;
                mode_manager_switch_to(mode_idx);
                audio_play_chime_mode(mode_idx);
                led_effects_mode_switch(mode_idx);
            }
            break;
        }
    }

    if (!matched) {
        printf(">>> [TRẠNG THÁI]: Thẻ mới chưa được gán tính năng!\r\n");
        printf(">>> [HƯỚNG DẪN]: Copy mã UID \"%s\" vào mảng s_magic_cards trong main.c để đặt tên & gán chế độ!\r\n", uid_compact);
        printf("======================================================\r\n\n");

        // Âm thanh và hiệu ứng mừng khi quét được thẻ mới
        audio_play_action_feedback();
        led_effects_color_burst(0, 255, 180); // Chớp ngọc lam rực rỡ
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

    // 2.2 Initialize Wireless OTA Background Subsystem (Wi-Fi & HTTP OTA Server)
    wifi_ota_init();

    // 3. Initialize WS2812B RGB LED Strip (GPIO 4 via RMT)
    ESP_ERROR_CHECK(led_effects_init());

    // 3.1 Initialize Smart Power Manager (Auto-dim, Sleep & Goodbye sound)
    const power_manager_cbs_t pm_callbacks = {
        .on_pre_sleep   = on_pm_pre_sleep,
        .on_enter_sleep = on_pm_enter_sleep,
        .on_wake_up     = on_pm_wake_up,
        .can_sleep      = on_pm_can_sleep
    };
    ESP_ERROR_CHECK(power_manager_init(&pm_callbacks));

    // 4. Initialize 6 Push Buttons (BTN 1..6 on GPIO 41, 21, 48, 42, 45, 47)
    ESP_ERROR_CHECK(button_driver_init(on_button_event));

    // 5. Khởi tạo Mode Manager & Đăng ký toàn bộ 10 chế độ
    ESP_ERROR_CHECK(mode_manager_init());

    // 5.1 Phát âm thanh khởi động đồ chơi vui tươi, chưa vào chế độ nào cả!
    if (!sb_play_startup()) {
        audio_play_startup_sound();
    }
    led_effects_startup();

    // 5.1 Khởi tạo module NFC / RFID PN532 (I2C: SDA=GPIO 6, SCL=GPIO 5)
    // pn532_driver_init(on_rfid_card_event);  // TẠM TẮT - khi nào PN532 kết nối được thì bật lại

    // 6. Start toy background monitor task (gọi led_effects_tick & mode_manager_tick)
    xTaskCreate(toy_background_task, "toy_bg", 3072, NULL, 4, NULL);

    // 7. Start USB Host Library & Drivers (Core 0)
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

    const msc_host_driver_config_t msc_host_driver_config = {
        .create_backround_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = msc_host_event_callback,
        .callback_arg = NULL
    };
    ESP_ERROR_CHECK(msc_host_install(&msc_host_driver_config));

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, "USB HOST READY! Cam ban phim USB hoac USB Flash Drive vao Type-C");
    ESP_LOGI(TAG, "==========================================================");

    bool initial_mode_set = false;
    TickType_t wait_ticks = pdMS_TO_TICKS(1200); // Chờ 1.2s xem có USB Flash Drive cắm sẵn lúc bật nguồn không

    app_event_queue_t evt_queue;
    while (1) {
        if (xQueueReceive(app_event_queue, &evt_queue, wait_ticks)) {
            if (APP_EVENT_HID_HOST == evt_queue.event_group) {
                hid_host_device_event(evt_queue.hid_host_device.handle,
                                      evt_queue.hid_host_device.event,
                                      evt_queue.hid_host_device.arg);
            } else if (APP_EVENT_UPDATE_LEDS == evt_queue.event_group) {
                update_keyboard_leds();
            } else if (APP_EVENT_BUTTON_PRESS == evt_queue.event_group) {
                button_id_t btn = evt_queue.button.id;
                button_press_type_t ptype = evt_queue.button.type;

                // Bất kỳ nút nào được bấm cũng nuôi bộ đếm rảnh và đánh thức hệ thống nếu đang ngủ
                power_manager_feed_activity();

                switch (btn) {
                    case BUTTON_VOL_DOWN: {
                        audio_synth_volume_down();
                        float vol = audio_synth_get_volume();
                        printf("\r\n>>> [BTN 1 - GPIO %d: GIẢM ÂM LƯỢNG -> %d%%]\r\n", PIN_BTN_VOL_DOWN, (int)(vol * 100));
                        audio_play_volume_tick(vol, false);
                        led_effects_volume(vol);
                        break;
                    }
                    case BUTTON_VOL_UP: {
                        audio_synth_volume_up();
                        float vol = audio_synth_get_volume();
                        printf("\r\n>>> [BTN 2 - GPIO %d: TĂNG ÂM LƯỢNG -> %d%%]\r\n", PIN_BTN_VOL_UP, (int)(vol * 100));
                        audio_play_volume_tick(vol, true);
                        led_effects_volume(vol);
                        break;
                    }
                    case BUTTON_MODE: {
                        if (mode_manager_is_in_standby()) {
                            printf("\r\n>>> [BTN 3: KHỞI ĐỘNG VÀO CHẾ ĐỘ 1: ĐÀN PIANO] <<<\r\n");
                            mode_manager_switch_to(0);
                        } else {
                            if (ptype == BUTTON_LONG_PRESS) {
                                printf("\r\n>>> [BTN 3 - GPIO %d NHẤN GIỮ: CHUYỂN TUẦN TỰ KẾ TIẾP]\r\n", PIN_BTN_MODE);
                                mode_manager_next();
                            } else {
                                printf("\r\n>>> [BTN 3 - GPIO %d NHẤN: CHUYỂN CHẾ ĐỘ NGẪU NHIÊN]\r\n", PIN_BTN_MODE);
                                mode_manager_random();
                            }
                        }
                        break;
                    }
                    case BUTTON_LED_MODE: {
                        led_style_t new_style = led_effects_next_style();
                        printf("\r\n>>> [BTN 4 - GPIO %d: ĐỔI HIỆU ỨNG ĐÈN LED -> %s] <<<\r\n", PIN_BTN_LED_MODE, led_effects_get_style_name(new_style));
                        audio_play_led_mode_feedback(new_style);
                        break;
                    }
                    case BUTTON_MUTE_OFF: {
                        if (ptype == BUTTON_LONG_PRESS) {
                            printf("\r\n>>> [BTN 5 - GPIO %d NHẤN GIỮ: CHỦ ĐỘNG VÀO CHẾ ĐỘ NGỦ (SLEEP)] <<<\r\n", PIN_BTN_MUTE_OFF);
                            power_manager_force_sleep();
                            break;
                        }
                        if (mode_manager_get_current_index() == 9) {
                            mp3_player_toggle_pause();
                            if (mp3_player_get_state() == MP3_STATE_PAUSED) {
                                printf("\r\n>>> [BTN 5 - GPIO %d: TẠM DỪNG NHẠC (PAUSE)] <<<\r\n", PIN_BTN_MUTE_OFF);
                                led_effects_color_burst(255, 200, 0);
                            } else {
                                printf("\r\n>>> [BTN 5 - GPIO %d: TIẾP TỤC PHÁT (RESUME)] <<<\r\n", PIN_BTN_MUTE_OFF);
                                led_effects_color_burst(0, 255, 100);
                            }
                            break;
                        }
                        bool muted = audio_synth_toggle_mute();
                        if (muted) {
                            printf("\r\n>>> [BTN 5 - GPIO %d: TẮT TIẾNG (MUTE / OFF)] <<<\r\n", PIN_BTN_MUTE_OFF);
                            led_effects_color_burst(255, 0, 0); // Đỏ báo tắt tiếng
                        } else {
                            float vol = audio_synth_get_volume();
                            printf("\r\n>>> [BTN 5 - GPIO %d: BẬT LẠI TIẾNG (UNMUTE: %d%%)] <<<\r\n", PIN_BTN_MUTE_OFF, (int)(vol * 100));
                            led_effects_volume(vol);
                        }
                        break;
                    }
                    case BUTTON_ACTION_SONG: {
                        if (ptype == BUTTON_LONG_PRESS && mode_manager_get_current_index() == 9) {
                            mp3_player_toggle_pause();
                            if (mp3_player_get_state() == MP3_STATE_PAUSED) {
                                printf("\r\n>>> [BTN 6 NHẤN GIỮ: TẠM DỪNG NHẠC (PAUSE)] <<<\r\n");
                                led_effects_color_burst(255, 200, 0);
                            } else {
                                printf("\r\n>>> [BTN 6 NHẤN GIỮ: TIẾP TỤC PHÁT (RESUME)] <<<\r\n");
                                led_effects_color_burst(0, 255, 100);
                            }
                            break;
                        }
                        printf("\r\n>>> [BTN 6 - GPIO %d: KÍCH HOẠT ACTION / ĐỔI BÀI HÁT] <<<\r\n", PIN_BTN_ACTION_SONG);
                        mode_manager_dispatch_action();
                        break;
                    }
                    default:
                        break;
                }
                fflush(stdout);
            } else if (APP_EVENT_MSC_CONNECTED == evt_queue.event_group) {
                ESP_LOGI(TAG, "Connecting to USB MSC device at addr %d...", evt_queue.msc_dev.address);
                esp_err_t err = msc_host_install_device(evt_queue.msc_dev.address, &s_msc_device_handle);
                if (err == ESP_OK) {
                    const esp_vfs_fat_mount_config_t mount_config = {
                        .format_if_mount_failed = false,
                        .max_files = 8,
                        .allocation_unit_size = 16 * 1024
                    };
                    err = msc_host_vfs_register(s_msc_device_handle, "/usb", &mount_config, &s_msc_vfs_handle);
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG, "USB Flash Drive mounted at /usb successfully!");
                        mp3_player_set_usb_mounted(true);
                        int count = mp3_player_scan_usb("/usb");
                        printf("\r\n>>> [USB ĐÃ CẮM: TÌM THẤY %d BÀI HÁT TẠI /usb] <<<\r\n", count);
                        if (mp3_player_is_usb_mounted() && s_msc_device_handle != NULL) {
                            s_prev_mode_before_usb = initial_mode_set ? mode_manager_get_current_index() : 0;
                            initial_mode_set = true;
                            wait_ticks = portMAX_DELAY;
                            mode_manager_switch_to(9); // Chuyển sang Mode 10 (Chỉ mục 9)
                        } else {
                            ESP_LOGW(TAG, "USB disconnected during scan, skipping mode switch");
                        }
                    } else {
                        ESP_LOGE(TAG, "Failed to mount /usb: %s", esp_err_to_name(err));
                        msc_host_uninstall_device(s_msc_device_handle);
                        s_msc_device_handle = NULL;
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to install MSC device: %s", esp_err_to_name(err));
                }
            } else if (APP_EVENT_MSC_DISCONNECTED == evt_queue.event_group) {
                printf("\r\n>>> [USB FLASH DRIVE ĐÃ RÚT RA] <<<\r\n");
                mp3_player_stop_sync();
                mp3_player_set_usb_mounted(false);
                mp3_player_clear_playlist();
                if (s_msc_vfs_handle) {
                    msc_host_vfs_unregister(s_msc_vfs_handle);
                    s_msc_vfs_handle = NULL;
                }
                if (s_msc_device_handle) {
                    msc_host_uninstall_device(s_msc_device_handle);
                    s_msc_device_handle = NULL;
                }
                if (mode_manager_get_current_index() == 9) {
                    mode_manager_switch_to(s_prev_mode_before_usb);
                }
            }
        } else {
            // Hết 1.2s chờ ban đầu nếu không có USB cắm sẵn: Duy trì trạng thái chờ (Standby)
            if (!initial_mode_set) {
                initial_mode_set = true;
                wait_ticks = portMAX_DELAY;
                printf("\r\n======================================================\r\n");
                printf(">>> [HỆ THỐNG ĐÃ KHỞI ĐỘNG XONG - TRẠNG THÁI CHỜ] <<<\r\n");
                printf(">>> Hãy nhấn nút CHUYỂN CHẾ ĐỘ (BTN 3) để vào Chế độ 1! <<<\r\n");
                printf("======================================================\r\n\n");
            }
        }
    }
}
