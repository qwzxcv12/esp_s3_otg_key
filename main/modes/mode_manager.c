#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mode_manager.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"

static const char *TAG = "mode_mgr";

static const toy_mode_driver_t *s_modes[MAX_TOY_MODES] = { 0 };
static uint8_t s_mode_count = 0;
static uint8_t s_current_index = 0;

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

    // Đăng ký toàn bộ các mode từ bảng Registry
    mode_registry_register_all();

    // Gọi hàm init của từng mode (nếu có)
    for (uint8_t i = 0; i < s_mode_count; i++) {
        if (s_modes[i] && s_modes[i]->init) {
            s_modes[i]->init();
        }
    }

    ESP_LOGI(TAG, "Mode Manager initialized with %d modes", s_mode_count);
    return ESP_OK;
}

void mode_manager_switch_to(uint8_t mode_index) {
    if (s_mode_count == 0) return;
    uint8_t new_idx = mode_index % s_mode_count;

    const toy_mode_driver_t *prev = s_modes[s_current_index];
    const toy_mode_driver_t *curr = s_modes[new_idx];

    // 1. Gọi hook on_exit của chế độ cũ (để tắt còi cứu hỏa, tắt autoplay, dừng timer...)
    if (prev && prev->on_exit) {
        prev->on_exit();
    }

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
}

void mode_manager_next(void) {
    if (s_mode_count == 0) return;
    mode_manager_switch_to((s_current_index + 1) % s_mode_count);
}

void mode_manager_prev(void) {
    if (s_mode_count == 0) return;
    mode_manager_switch_to((s_current_index + s_mode_count - 1) % s_mode_count);
}

bool mode_manager_dispatch_key(uint8_t keycode, uint8_t modifier, char ascii_char) {
    if (s_mode_count == 0) return false;
    const toy_mode_driver_t *curr = s_modes[s_current_index];
    if (curr && curr->handle_key) {
        return curr->handle_key(keycode, modifier, ascii_char);
    }
    return false;
}

void mode_manager_dispatch_release(uint8_t keycode) {
    if (s_mode_count == 0) return;
    const toy_mode_driver_t *curr = s_modes[s_current_index];
    if (curr && curr->handle_key_release) {
        curr->handle_key_release(keycode);
    }
}

void mode_manager_tick(uint32_t delta_ms) {
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
