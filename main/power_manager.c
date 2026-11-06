#include "power_manager.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "led_effects.h"

static const char *TAG = "POWER_MGR";

static power_state_t s_state = POWER_STATE_ACTIVE;
static power_manager_cbs_t s_cbs = { 0 };
static SemaphoreHandle_t s_mutex = NULL;

static uint32_t s_inactivity_timer_ms = 0;
static uint32_t s_pre_sleep_timer_ms = 0;
#define PRE_SLEEP_LULLABY_DURATION_MS 1600

// Quản lý độ sáng chuyển tiếp mượt mà (0..255)
#define BRIGHTNESS_ACTIVE     255
#define BRIGHTNESS_DIMMED     50    // ~20% độ sáng
#define BRIGHTNESS_PRE_SLEEP  25    // ~10% độ sáng
#define BRIGHTNESS_SLEEP      0     // 0% tắt hẳn

static uint8_t s_current_brightness = BRIGHTNESS_ACTIVE;
static uint8_t s_target_brightness  = BRIGHTNESS_ACTIVE;

const char *power_manager_get_state_name(power_state_t state) {
    switch (state) {
        case POWER_STATE_ACTIVE:    return "ACTIVE (HOẠT ĐỘNG)";
        case POWER_STATE_DIMMED:    return "DIMMED (TIẾT KIỆM SÁNG)";
        case POWER_STATE_PRE_SLEEP: return "PRE_SLEEP (CHUẨN BỊ NGỦ)";
        case POWER_STATE_SLEEP:     return "SLEEP (TỰ NGHỈ SÂU)";
        default:                    return "UNKNOWN";
    }
}

esp_err_t power_manager_init(const power_manager_cbs_t *callbacks) {
    if (callbacks) {
        s_cbs = *callbacks;
    }
    s_mutex = xSemaphoreCreateMutex();
    s_state = POWER_STATE_ACTIVE;
    s_inactivity_timer_ms = 0;
    s_pre_sleep_timer_ms = 0;
    s_current_brightness = BRIGHTNESS_ACTIVE;
    s_target_brightness = BRIGHTNESS_ACTIVE;
    led_effects_set_master_brightness(BRIGHTNESS_ACTIVE);

    ESP_LOGI(TAG, "Smart Power Manager Initialized (Dim: %ds, Sleep: %ds)",
             POWER_INACTIVITY_DIM_MS / 1000, POWER_INACTIVITY_SLEEP_MS / 1000);
    return ESP_OK;
}

bool power_manager_feed_activity(void) {
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;

    power_state_t prev_state = s_state;
    bool was_sleeping = (prev_state == POWER_STATE_SLEEP || prev_state == POWER_STATE_PRE_SLEEP);

    // Reset toàn bộ bộ đếm thời gian rảnh
    s_inactivity_timer_ms = 0;
    s_pre_sleep_timer_ms = 0;

    if (s_state != POWER_STATE_ACTIVE) {
        s_state = POWER_STATE_ACTIVE;
        s_target_brightness = BRIGHTNESS_ACTIVE;
        s_current_brightness = BRIGHTNESS_ACTIVE; // Bật sáng lại ngay lập tức
        led_effects_set_master_brightness(BRIGHTNESS_ACTIVE);
        led_effects_reset_idle();

        ESP_LOGI(TAG, ">>> [WAKE UP / TƯƠNG TÁC PHÍM] Chuyển từ %s -> ACTIVE",
                 power_manager_get_state_name(prev_state));

        if (s_cbs.on_state_change) {
            s_cbs.on_state_change(prev_state, POWER_STATE_ACTIVE);
        }

        if (was_sleeping && s_cbs.on_wake_up) {
            s_cbs.on_wake_up();
        }
    }

    xSemaphoreGive(s_mutex);
    return was_sleeping;
}

void power_manager_force_sleep(void) {
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;

    if (s_state != POWER_STATE_SLEEP && s_state != POWER_STATE_PRE_SLEEP) {
        power_state_t old = s_state;
        s_state = POWER_STATE_PRE_SLEEP;
        s_target_brightness = BRIGHTNESS_PRE_SLEEP;
        s_pre_sleep_timer_ms = 0;

        ESP_LOGI(TAG, ">>> [FORCE SLEEP] Kích hoạt chế độ ngủ chủ động");
        if (s_cbs.on_state_change) s_cbs.on_state_change(old, POWER_STATE_PRE_SLEEP);
        if (s_cbs.on_pre_sleep) s_cbs.on_pre_sleep();
    }

    xSemaphoreGive(s_mutex);
}

void power_manager_force_wake(void) {
    power_manager_feed_activity();
}

void power_manager_tick(uint32_t delta_ms) {
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    // 1. Cập nhật chuyển tiếp mượt mà độ sáng LED (Smooth Brightness Interpolation)
    if (s_current_brightness != s_target_brightness) {
        if (s_current_brightness < s_target_brightness) {
            // Tăng độ sáng nhanh (bật đèn khi tương tác)
            int next = (int)s_current_brightness + 18;
            s_current_brightness = (next > s_target_brightness) ? s_target_brightness : (uint8_t)next;
        } else {
            // Giảm độ sáng từ từ, êm dịu mắt
            int next = (int)s_current_brightness - 4;
            s_current_brightness = (next < s_target_brightness) ? s_target_brightness : (uint8_t)next;
        }
        led_effects_set_master_brightness(s_current_brightness);
    }

    // 2. Máy trạng thái quản lý thời gian rảnh
    switch (s_state) {
        case POWER_STATE_ACTIVE:
            s_inactivity_timer_ms += delta_ms;
            if (s_inactivity_timer_ms >= POWER_INACTIVITY_DIM_MS) {
                power_state_t old = s_state;
                s_state = POWER_STATE_DIMMED;
                s_target_brightness = BRIGHTNESS_DIMMED;
                ESP_LOGI(TAG, ">>> [TIẾT KIỆM SÁNG] Không thao tác sau %ds -> DIMMED (%d%%)",
                         POWER_INACTIVITY_DIM_MS / 1000, (BRIGHTNESS_DIMMED * 100) / 255);
                if (s_cbs.on_state_change) s_cbs.on_state_change(old, POWER_STATE_DIMMED);
            }
            break;

        case POWER_STATE_DIMMED:
            s_inactivity_timer_ms += delta_ms;
            if (s_inactivity_timer_ms >= POWER_INACTIVITY_SLEEP_MS) {
                power_state_t old = s_state;
                s_state = POWER_STATE_PRE_SLEEP;
                s_target_brightness = BRIGHTNESS_PRE_SLEEP;
                s_pre_sleep_timer_ms = 0;
                ESP_LOGI(TAG, ">>> [CHUẨN BỊ NGỦ] Không thao tác sau %ds -> PRE_SLEEP (Phát âm tạm biệt)",
                         POWER_INACTIVITY_SLEEP_MS / 1000);
                if (s_cbs.on_state_change) s_cbs.on_state_change(old, POWER_STATE_PRE_SLEEP);
                if (s_cbs.on_pre_sleep) s_cbs.on_pre_sleep();
            }
            break;

        case POWER_STATE_PRE_SLEEP:
            s_pre_sleep_timer_ms += delta_ms;
            // Chờ âm thanh tạm biệt phát xong hoàn toàn (khoảng 1.6s)
            if (s_pre_sleep_timer_ms >= PRE_SLEEP_LULLABY_DURATION_MS) {
                power_state_t old = s_state;
                s_state = POWER_STATE_SLEEP;
                s_target_brightness = BRIGHTNESS_SLEEP;
                s_current_brightness = BRIGHTNESS_SLEEP;
                led_effects_set_master_brightness(BRIGHTNESS_SLEEP);
                led_effects_clear();

                ESP_LOGI(TAG, ">>> [VÀO CHẾ ĐỘ NGỦ] Tắt toàn bộ LED, ngắt clock I2S, tắt đèn bàn phím");
                if (s_cbs.on_state_change) s_cbs.on_state_change(old, POWER_STATE_SLEEP);
                if (s_cbs.on_enter_sleep) s_cbs.on_enter_sleep();
            }
            break;

        case POWER_STATE_SLEEP:
            // Duy trì trạng thái ngủ sâu, dải LED tắt hoàn toàn
            break;
    }

    xSemaphoreGive(s_mutex);
}

power_state_t power_manager_get_state(void) {
    return s_state;
}

bool power_manager_is_sleeping(void) {
    return (s_state == POWER_STATE_SLEEP || s_state == POWER_STATE_PRE_SLEEP);
}

bool power_manager_is_dimmed(void) {
    return (s_state == POWER_STATE_DIMMED);
}

uint8_t power_manager_get_brightness(void) {
    return s_current_brightness;
}
