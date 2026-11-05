#include "button_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUTTON_DRIVER";

static const int s_button_pins[BUTTON_TOTAL_COUNT] = {
    [BUTTON_VOL_DOWN]    = PIN_BTN_VOL_DOWN,
    [BUTTON_VOL_UP]      = PIN_BTN_VOL_UP,
    [BUTTON_MODE]        = PIN_BTN_MODE,
    [BUTTON_LED_MODE]    = PIN_BTN_LED_MODE,
    [BUTTON_MUTE_OFF]    = PIN_BTN_MUTE_OFF,
    [BUTTON_ACTION_SONG] = PIN_BTN_ACTION_SONG
};

static const char *s_button_names[BUTTON_TOTAL_COUNT] = {
    [BUTTON_VOL_DOWN]    = "BTN 1 (VOL_DOWN)",
    [BUTTON_VOL_UP]      = "BTN 2 (VOL_UP)",
    [BUTTON_MODE]        = "BTN 3 (MODE_CHUYEN)",
    [BUTTON_LED_MODE]    = "BTN 4 (LED_MODE)",
    [BUTTON_MUTE_OFF]    = "BTN 5 (MUTE/OFF)",
    [BUTTON_ACTION_SONG] = "BTN 6 (ACTION/SONG)"
};

const char *button_driver_get_name(button_id_t btn) {
    if (btn < BUTTON_TOTAL_COUNT && s_button_names[btn]) {
        return s_button_names[btn];
    }
    return "UNKNOWN_BTN";
}

typedef struct {
    int pin;
    uint8_t debounce_cnt;
    bool is_pressed;
    bool long_emitted;
    bool repeat_emitted;
    uint32_t press_ms;
    uint32_t repeat_ms;
} button_state_t;

static button_state_t s_states[BUTTON_TOTAL_COUNT];
static button_event_cb_t s_event_cb = NULL;

static void button_scanner_task(void *arg) {
    ESP_LOGI(TAG, "6-Button Scanner Task running at 100Hz (10ms tick)");

    while (1) {
        for (int i = 0; i < BUTTON_TOTAL_COUNT; i++) {
            button_state_t *st = &s_states[i];
            // Active-Low: 0 là đang bấm, 1 là đang nhả
            int raw_level = gpio_get_level(st->pin);
            bool raw_pressed = (raw_level == 0);

            // Chống rung Debounce (3 chu kỳ 10ms = 30ms ổn định)
            if (raw_pressed != st->is_pressed) {
                st->debounce_cnt++;
                if (st->debounce_cnt >= 3) {
                    st->is_pressed = raw_pressed;
                    st->debounce_cnt = 0;

                    if (st->is_pressed) {
                        // Vừa mới nhấn xuống
                        st->press_ms = 0;
                        st->repeat_ms = 0;
                        st->long_emitted = false;
                        st->repeat_emitted = false;

                        // Đối với Vol+ / Vol-, bắn luôn CLICK lần đầu tiên ngay khi bấm
                        if (i == BUTTON_VOL_DOWN || i == BUTTON_VOL_UP) {
                            if (s_event_cb) {
                                s_event_cb((button_id_t)i, BUTTON_CLICK);
                            }
                        }
                    } else {
                        // Vừa mới nhả nút
                        if (!st->long_emitted && !st->repeat_emitted) {
                            // Nếu chưa kích hoạt long press và không phải đang repeat thì bắn CLICK
                            if (i != BUTTON_VOL_DOWN && i != BUTTON_VOL_UP) {
                                if (s_event_cb) {
                                    s_event_cb((button_id_t)i, BUTTON_CLICK);
                                }
                            }
                        }
                    }
                }
            } else {
                st->debounce_cnt = 0;
            }

            // Xử lý khi nút đang được giữ (is_pressed == true)
            if (st->is_pressed) {
                st->press_ms += 10;

                // Nút Vol+/Vol-: Giữ lâu > 350ms thì tự động lặp lại (Repeat mỗi 100ms)
                if (i == BUTTON_VOL_DOWN || i == BUTTON_VOL_UP) {
                    if (st->press_ms >= 350) {
                        st->repeat_ms += 10;
                        if (st->repeat_ms >= 100) {
                            st->repeat_ms = 0;
                            st->repeat_emitted = true;
                            if (s_event_cb) {
                                s_event_cb((button_id_t)i, BUTTON_REPEAT);
                            }
                        }
                    }
                } else {
                    // Các nút chức năng khác (MODE, LED, MUTE, ACTION): Phát hiện Long Press >= 1000ms
                    if (st->press_ms >= 1000 && !st->long_emitted) {
                        st->long_emitted = true;
                        if (s_event_cb) {
                            s_event_cb((button_id_t)i, BUTTON_LONG_PRESS);
                        }
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t button_driver_init(button_event_cb_t callback) {
    s_event_cb = callback;

    uint64_t pin_mask = 0;
    for (int i = 0; i < BUTTON_TOTAL_COUNT; i++) {
        pin_mask |= (1ULL << s_button_pins[i]);
        s_states[i].pin = s_button_pins[i];
        s_states[i].debounce_cnt = 0;
        s_states[i].is_pressed = false;
        s_states[i].long_emitted = false;
        s_states[i].repeat_emitted = false;
        s_states[i].press_ms = 0;
        s_states[i].repeat_ms = 0;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Button GPIO pins: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ret = xTaskCreate(button_scanner_task, "btn_scan", 3072, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button scanner task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "6-Button Driver initialized successfully!");
    for (int i = 0; i < BUTTON_TOTAL_COUNT; i++) {
        ESP_LOGI(TAG, "  [%s] -> GPIO %d", s_button_names[i], s_button_pins[i]);
    }

    return ESP_OK;
}
