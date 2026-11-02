#include "rotary_encoder.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ENCODER_HW040";

static encoder_rotate_cb_t s_rotate_cb = NULL;
static encoder_btn_cb_t s_click_cb = NULL;

static void rotary_encoder_task(void *arg) {
    ESP_LOGI(TAG, "Rotary Encoder active on CLK=%d, DT=%d, SW=%d",
             PIN_ENCODER_CLK, PIN_ENCODER_DT, PIN_ENCODER_SW);

    // Initial state: (CLK << 1) | DT
    uint8_t last_state = (gpio_get_level(PIN_ENCODER_CLK) << 1) | gpio_get_level(PIN_ENCODER_DT);
    int8_t step_accum = 0;

    int last_sw = gpio_get_level(PIN_ENCODER_SW);
    int64_t last_sw_time = 0;

    // Full 16-entry Gray-code quadrature transition lookup table
    // Index: (last_state << 2) | curr_state
    static const int8_t table[16] = {
        0, -1,  1,  0,
        1,  0,  0, -1,
       -1,  0,  0,  1,
        0,  1, -1,  0
    };

    while (1) {
        // 1. Quadrature Rotary Tracking
        uint8_t curr_state = (gpio_get_level(PIN_ENCODER_CLK) << 1) | gpio_get_level(PIN_ENCODER_DT);
        if (curr_state != last_state) {
            int8_t delta = table[(last_state << 2) | curr_state];
            step_accum += delta;
            last_state = curr_state;

            // Trigger when reaching detent resting positions (State 3: both HIGH, or State 0)
            if (curr_state == 3 || curr_state == 0) {
                if (step_accum >= 2) {
                    if (s_rotate_cb) s_rotate_cb(1);
                    step_accum = 0;
                } else if (step_accum <= -2) {
                    if (s_rotate_cb) s_rotate_cb(-1);
                    step_accum = 0;
                } else {
                    step_accum = 0;
                }
            }
        }

        // 2. Push button (SW) with debouncing
        int curr_sw = gpio_get_level(PIN_ENCODER_SW);
        int64_t now_ms = esp_timer_get_time() / 1000;

        // Button is active-low (pulled HIGH, pressed LOW)
        if (curr_sw == 0 && last_sw == 1) {
            if (now_ms - last_sw_time > 200) { // 200ms debounce
                last_sw_time = now_ms;
                if (s_click_cb) s_click_cb();
            }
        }
        last_sw = curr_sw;

        vTaskDelay(pdMS_TO_TICKS(2)); // 500 Hz sampling rate
    }
}

esp_err_t rotary_encoder_init(encoder_rotate_cb_t on_rotate, encoder_btn_cb_t on_press) {
    s_rotate_cb = on_rotate;
    s_click_cb = on_press;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_ENCODER_CLK) | (1ULL << PIN_ENCODER_DT) | (1ULL << PIN_ENCODER_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO pins: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(rotary_encoder_task, "rotary_enc", 3072, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create rotary encoder task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "HW-040 Rotary Encoder initialized on CLK=%d, DT=%d, SW=%d",
             PIN_ENCODER_CLK, PIN_ENCODER_DT, PIN_ENCODER_SW);
    return ESP_OK;
}
