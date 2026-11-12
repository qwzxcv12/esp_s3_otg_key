#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mode_manager.h"
#include "mode_demo.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"
#include "mp3_player.h"

static const char *TAG = "mode_mgr";

static const toy_mode_driver_t *s_modes[MAX_TOY_MODES] = { 0 };
static uint8_t s_mode_count = 0;
static uint8_t s_current_index = 0;
static bool    s_in_standby = true;

// Các driver chế độ khai báo extern từ mode_registry.c hoặc từng file mode
extern void mode_registry_register_all(void);

esp_err_t mode_manager_register(const toy_mode_driver_t *driver) {
    if (!driver) return ESP_ERR_INVALID_ARG;
    if (s_mode_count >= MAX_TOY_MODES) {
        ESP_LOGE(TAG, "Mode registry full (max %d)", MAX_TOY_MODES);
        return ESP_ERR_NO_MEM;
    }

    s_modes[s_mode_count++] = driver;
    ESP_LOGI(TAG, "Registered Mode [%d]: %s", driver->id, driver->name ? driver->name : "Unnamed");
    return ESP_OK;
}

esp_err_t mode_manager_init(void) {
    s_mode_count = 0;
    s_current_index = 0;
    s_in_standby = true;

    // Đăng ký toàn bộ các mode từ bảng Registry
    mode_registry_register_all();

    // Gọi hàm init của từng mode (nếu có)
    for (uint8_t i = 0; i < s_mode_count; i++) {
        if (s_modes[i] && s_modes[i]->init) {
            s_modes[i]->init();
        }
    }

    // Khởi tạo hệ thống biểu diễn Demo 30s
    mode_demo_init();

    ESP_LOGI(TAG, "Mode Manager initialized with %d modes (Standby state)", s_mode_count);
    return ESP_OK;
}

bool mode_manager_is_in_standby(void) {
    return s_in_standby;
}

void mode_manager_enter_standby(void) {
    mode_demo_stop();
    if (!s_in_standby && s_modes[s_current_index] && s_modes[s_current_index]->on_exit) {
        s_modes[s_current_index]->on_exit();
    }
    s_in_standby = true;
    audio_synth_stop_all_background();
    led_effects_standby(true);
    printf("\r\n>>> [HỆ THỐNG VÀO TRẠNG THÁI CHỜ - BẤM NÚT CHẾ ĐỘ ĐỂ BẮT ĐẦU] <<<\r\n");
}

void mode_manager_switch_to(uint8_t mode_index) {
    if (s_mode_count == 0) return;
    uint8_t new_idx = mode_index % s_mode_count;

    // Bảo vệ: Nếu chuyển vào Mode 10 (chỉ mục 9) nhưng chưa cắm USB Flash Drive
    if (new_idx == 9 && !mp3_player_is_usb_mounted()) {
        printf("\r\n>>> [MODE MANAGER]: Chưa cắm USB Flash Drive chứa nhạc! Không thể vào Chế độ 10 <<<\r\n");
        led_effects_color_burst(255, 120, 0);
        return;
    }

    // Dừng bản demo đang chạy (nếu có)
    mode_demo_stop();

    const toy_mode_driver_t *prev = s_modes[s_current_index];
    const toy_mode_driver_t *curr = s_modes[new_idx];

    // 1. Gọi hook on_exit của chế độ cũ nếu trước đó đang ở trong một chế độ
    if (!s_in_standby && prev && prev->on_exit) {
        prev->on_exit();
    }

    s_in_standby = false;
    led_effects_standby(false);
    s_current_index = new_idx;

    printf("\r\n\r\n========================================================\r\n");
    printf(">>> [CHUYỂN CHẾ ĐỘ: %s] <<<\r\n", curr && curr->name ? curr->name : "CHẾ ĐỘ MỚI");
    printf("========================================================\r\n");
    fflush(stdout);

    // 2. Dọn dẹp âm thanh nền, cập nhật soundbank và phát thông báo tên chế độ
    audio_synth_stop_all_background();
    audio_mozart_set_autoplay(false);
    soundbank_set_mode(s_current_index);

    if (!sb_play_mode_name(s_current_index)) {
        if (!sb_play_chime(s_current_index)) {
            audio_play_chime_mode(s_current_index);
        }
    }

    // 3. Hiệu ứng đổi chế độ trên dải LED
    led_effects_mode_switch(s_current_index);

    // 4. Gọi hook on_enter của chế độ mới (in hướng dẫn, reset state...)
    if (curr && curr->on_enter) {
        curr->on_enter();
    }

    // 5. Khởi động bản Demo 30s đặc sắc cho chế độ này!
    mode_demo_start(s_current_index);
}

void mode_manager_random(void) {
    if (s_mode_count == 0) return;
    if (s_in_standby) {
        mode_manager_switch_to(0);
        return;
    }

    uint8_t max_modes = s_mode_count;
    if (!mp3_player_is_usb_mounted() && max_modes > 9) {
        max_modes = 9; // Không random vào Mode 10 nếu chưa cắm USB
    }
    if (max_modes <= 1) {
        mode_manager_switch_to(0);
        return;
    }

    uint8_t next_idx = s_current_index;
    for (int retry = 0; retry < 10; retry++) {
        uint8_t candidate = (uint8_t)(esp_random() % max_modes);
        if (candidate != s_current_index) {
            next_idx = candidate;
            break;
        }
    }
    if (next_idx == s_current_index) {
        next_idx = (s_current_index + 1) % max_modes;
    }

    mode_manager_switch_to(next_idx);
}

void mode_manager_next(void) {
    if (s_mode_count == 0) return;
    if (s_in_standby) {
        mode_manager_switch_to(0);
        return;
    }
    uint8_t next_idx = (s_current_index + 1) % s_mode_count;
    // Nếu chưa cắm USB, bỏ qua Mode 10 (chỉ mục 9) và quay về Mode 1 (chỉ mục 0)
    if (next_idx == 9 && !mp3_player_is_usb_mounted()) {
        next_idx = 0;
    }
    mode_manager_switch_to(next_idx);
}

void mode_manager_prev(void) {
    if (s_mode_count == 0) return;
    if (s_in_standby) {
        mode_manager_switch_to(0);
        return;
    }
    uint8_t prev_idx = (s_current_index + s_mode_count - 1) % s_mode_count;
    // Nếu chưa cắm USB, bỏ qua Mode 10 (chỉ mục 9) và lùi về Mode 9 (chỉ mục 8)
    if (prev_idx == 9 && !mp3_player_is_usb_mounted()) {
        prev_idx = 8;
    }
    mode_manager_switch_to(prev_idx);
}

bool mode_manager_dispatch_key(uint8_t keycode, uint8_t modifier, char ascii_char) {
    if (s_in_standby) {
        audio_play_praise();
        led_effects_color_burst(0, 200, 255);
        printf("\r\n>>> [BÉ ĐANG Ở MÀN HÌNH CHỜ: HÃY BẤM NÚT 'CHUYỂN CHẾ ĐỘ' (BTN 3) ĐỂ BẮT ĐẦU CHƠI NHÉ!] <<<\r\n");
        return true;
    }

    // Nếu đang phát bản Demo 30s: dừng demo ngay lập tức để bé gõ phím biểu diễn tự do!
    if (mode_demo_is_active()) {
        mode_demo_stop();
    }

    if (s_mode_count == 0) return false;
    const toy_mode_driver_t *curr = s_modes[s_current_index];
    if (curr && curr->handle_key) {
        return curr->handle_key(keycode, modifier, ascii_char);
    }
    return false;
}

void mode_manager_dispatch_release(uint8_t keycode) {
    if (s_in_standby) return;
    if (s_mode_count == 0) return;
    const toy_mode_driver_t *curr = s_modes[s_current_index];
    if (curr && curr->handle_key_release) {
        curr->handle_key_release(keycode);
    }
}

void mode_manager_dispatch_action(void) {
    if (s_in_standby) {
        mode_manager_switch_to(0);
        return;
    }

    // Nút ACTION (BTN 6): nếu đang phát Demo 30s thì dừng, nếu đang dừng thì bật lại Demo 30s!
    if (mode_demo_is_active()) {
        mode_demo_stop();
    } else {
        printf("\r\n>>> [BTN 6 ACTION: KÍCH HOẠT LẠI DEMO 30S ĐẶC SẮC] <<<\r\n");
        mode_demo_start(s_current_index);
    }
}

void mode_manager_tick(uint32_t delta_ms) {
    if (s_in_standby) return;

    // Cập nhật sequencer cho bản Demo 30s
    mode_demo_tick(delta_ms);

    if (s_mode_count == 0) return;
    const toy_mode_driver_t *curr = s_modes[s_current_index];
    if (curr && curr->background_tick) {
        curr->background_tick(delta_ms);
    }
}

const toy_mode_driver_t *mode_manager_get_current(void) {
    if (s_mode_count == 0) return NULL;
    return s_modes[s_current_index];
}

uint8_t mode_manager_get_current_index(void) {
    return s_current_index;
}

uint8_t mode_manager_get_count(void) {
    return s_mode_count;
}

const toy_mode_driver_t *mode_manager_get_driver(uint8_t index) {
    if (index >= s_mode_count) return NULL;
    return s_modes[index];
}
