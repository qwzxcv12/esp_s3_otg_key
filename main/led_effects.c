#include "led_effects.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "LED_FX";

static led_strip_handle_t s_led_strip = NULL;
static SemaphoreHandle_t s_led_mutex = NULL;
static uint32_t s_idle_step = 0;
static uint32_t s_idle_elapsed_ms = 0;

typedef enum {
    LED_ANIM_NONE = 0,
    LED_ANIM_MODE_SWITCH,
    LED_ANIM_CELEBRATION,
    LED_ANIM_COUNTING
} led_anim_type_t;

static volatile led_anim_type_t s_anim_type = LED_ANIM_NONE;
static volatile uint16_t s_anim_step = 0;
static volatile uint16_t s_anim_hue = 0;
static volatile uint8_t  s_count_target = 0;
static volatile uint8_t  s_count_current = 0;
static volatile int32_t  s_count_wait_ms = 0;
static volatile uint32_t s_count_step_ms = 200;

static inline void touch_led_activity(bool cancel_anim) {
    if (cancel_anim && s_anim_type != LED_ANIM_CELEBRATION) {
        s_anim_type = LED_ANIM_NONE;
    }
    s_idle_elapsed_ms = 0;
}

// Convert HSV (0-360, 0-100, 0-100) to RGB
static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    float hf = (float)h / 60.0f;
    float sf = (float)s / 100.0f;
    float vf = (float)v / 100.0f;
    int i = (int)hf;
    float f = hf - (float)i;
    float p = vf * (1.0f - sf);
    float q = vf * (1.0f - sf * f);
    float t = vf * (1.0f - sf * (1.0f - f));

    float rf = 0, gf = 0, bf = 0;
    switch (i % 6) {
        case 0: rf = vf; gf = t;  bf = p;  break;
        case 1: rf = q;  gf = vf; bf = p;  break;
        case 2: rf = p;  gf = vf; bf = t;  break;
        case 3: rf = p;  gf = q;  bf = vf; break;
        case 4: rf = t;  gf = p;  bf = vf; break;
        case 5: rf = vf; gf = p;  bf = q;  break;
    }
    *r = (uint8_t)(rf * 255.0f);
    *g = (uint8_t)(gf * 255.0f);
    *b = (uint8_t)(bf * 255.0f);
}

esp_err_t led_effects_init(void) {
    ESP_LOGI(TAG, "Initializing WS2812B LED strip on GPIO %d (%d LEDs)", PIN_LED_STRIP, LED_STRIP_COUNT);

    s_led_mutex = xSemaphoreCreateMutex();

    led_strip_config_t strip_config = {
        .strip_gpio_num = PIN_LED_STRIP,
        .max_leds = LED_STRIP_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT device for LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    led_strip_clear(s_led_strip);
    led_effects_mode_switch(0); // Non-blocking startup ripple
    ESP_LOGI(TAG, "WS2812B RMT driver initialized successfully!");
    return ESP_OK;
}

void led_effects_clear(void) {
    if (!s_led_strip || !s_led_mutex) return;
    s_anim_type = LED_ANIM_NONE;
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    led_strip_clear(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Non-blocking periodic animation tick (called every 25ms by toy_background_task)
void led_effects_tick(uint32_t delta_ms) {
    if (!s_led_strip || !s_led_mutex) return;

    led_anim_type_t anim = s_anim_type;

    if (anim == LED_ANIM_MODE_SWITCH) {
        uint16_t step = s_anim_step;
        uint16_t base_hue = s_anim_hue;
        if (step < LED_STRIP_COUNT + 6) {
            if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                for (int i = 0; i < LED_STRIP_COUNT; i++) {
                    if (i <= (int)step && ((int)step - i) < 7) {
                        uint8_t r, g, b;
                        hsv_to_rgb((base_hue + i * 8) % 360, 100, 80, &r, &g, &b);
                        led_strip_set_pixel(s_led_strip, i, r, g, b);
                    } else {
                        led_strip_set_pixel(s_led_strip, i, 0, 0, 0);
                    }
                }
                led_strip_refresh(s_led_strip);
                xSemaphoreGive(s_led_mutex);
            }
            s_anim_step = step + 2; // Fast smooth sweep across 30 LEDs
        } else {
            s_anim_type = LED_ANIM_NONE;
            s_idle_elapsed_ms = 0;
        }
        return;
    }

    if (anim == LED_ANIM_CELEBRATION) {
        uint16_t step = s_anim_step;
        if (step < 18) {
            if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                for (int i = 0; i < LED_STRIP_COUNT; i++) {
                    uint8_t r, g, b;
                    uint16_t hue = (step * 25 + i * 12) % 360;
                    hsv_to_rgb(hue, 100, 85, &r, &g, &b);
                    led_strip_set_pixel(s_led_strip, i, r, g, b);
                }
                led_strip_refresh(s_led_strip);
                xSemaphoreGive(s_led_mutex);
            }
            s_anim_step = step + 1;
        } else {
            s_anim_type = LED_ANIM_NONE;
            s_idle_elapsed_ms = 0;
        }
        return;
    }

    if (anim == LED_ANIM_COUNTING) {
        s_count_wait_ms -= (int32_t)delta_ms;
        if (s_count_wait_ms <= 0) {
            uint8_t cur = s_count_current + 1;
            s_count_current = cur;
            // Color progresses warmly along the strip
            uint8_t r, g, b;
            hsv_to_rgb((cur * 28) % 360, 95, 95, &r, &g, &b);
            led_effects_count(cur, r, g, b);
            if (cur >= s_count_target) {
                s_anim_type = LED_ANIM_NONE;
                s_idle_elapsed_ms = 0;
            } else {
                s_count_wait_ms = (int32_t)s_count_step_ms;
            }
        }
        return;
    }

    // Gentle idle breathing glow after 4.5 seconds of inactivity so the toy stays inviting
    s_idle_elapsed_ms += delta_ms;
    if (s_idle_elapsed_ms >= 4500) {
        led_effects_idle_step();
    }
}

// Mode switch wave animation (100% non-blocking: zero delay in USB HID callback!)
void led_effects_mode_switch(uint8_t mode_index) {
    s_anim_hue = (mode_index * 40) % 360;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_MODE_SWITCH;
    s_idle_elapsed_ms = 0;
}

// Celebration rainbow fanfare (100% non-blocking: zero delay in USB HID callback!)
void led_effects_celebration(void) {
    s_anim_step = 0;
    s_anim_type = LED_ANIM_CELEBRATION;
    s_idle_elapsed_ms = 0;
}

// Non-blocking synchronized LED counting sequence for Mode 9
void led_effects_start_counting(uint8_t target_count, uint32_t delay_ms, uint32_t step_ms) {
    if (target_count == 0) {
        s_anim_type = LED_ANIM_NONE;
        led_effects_color_burst(0, 200, 255);
        return;
    }
    if (target_count > LED_STRIP_COUNT) target_count = LED_STRIP_COUNT;
    s_count_target = target_count;
    s_count_current = 0;
    s_count_wait_ms = (int32_t)delay_ms;
    s_count_step_ms = (step_ms > 50) ? step_ms : 180;
    s_anim_type = LED_ANIM_COUNTING;
    s_idle_elapsed_ms = 0;
}

// Note hit visual: center around note_idx on the strip
void led_effects_note(uint8_t note_idx, uint8_t total_notes, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    int center = (note_idx * (LED_STRIP_COUNT - 1)) / (total_notes > 0 ? total_notes : 1);
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        int dist = abs(i - center);
        if (dist == 0) {
            led_strip_set_pixel(s_led_strip, i, r, g, b);
        } else if (dist == 1) {
            led_strip_set_pixel(s_led_strip, i, r / 3, g / 3, b / 3);
        } else {
            led_strip_set_pixel(s_led_strip, i, 0, 0, 0);
        }
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Drum beat strobe
void led_effects_beat(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        led_strip_set_pixel(s_led_strip, i, 180, 20, 20); // Bright Red strobe
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Typewriter mechanical spark
void led_effects_spark(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    int pos = rand() % LED_STRIP_COUNT;
    led_strip_clear(s_led_strip);
    led_strip_set_pixel(s_led_strip, pos, 255, 220, 100); // Warm yellow-white spark
    if (pos > 0) led_strip_set_pixel(s_led_strip, pos - 1, 80, 60, 20);
    if (pos < LED_STRIP_COUNT - 1) led_strip_set_pixel(s_led_strip, pos + 1, 80, 60, 20);
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Typewriter paper carriage bar (moves left to right, glows orange-red near margin bell)
void led_effects_carriage(uint8_t col, uint8_t max_col) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    if (max_col == 0) max_col = LED_STRIP_COUNT;
    int pos = (col * (LED_STRIP_COUNT - 1)) / max_col;
    if (pos >= LED_STRIP_COUNT) pos = LED_STRIP_COUNT - 1;

    led_strip_clear(s_led_strip);
    for (int i = 0; i <= pos; i++) {
        if (i == pos) {
            // Print head cursor: bright white-gold, or red-orange in margin bell warning zone
            if (pos >= LED_STRIP_COUNT - 5) {
                led_strip_set_pixel(s_led_strip, i, 255, 60, 0);
            } else {
                led_strip_set_pixel(s_led_strip, i, 255, 240, 180);
            }
        } else {
            // Typed paper trail: soft warm vintage amber
            led_strip_set_pixel(s_led_strip, i, 45, 30, 8);
        }
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Full strip color burst
void led_effects_color_burst(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        led_strip_set_pixel(s_led_strip, i, r, g, b);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Matrix green trail for Morse / Robot
void led_effects_matrix(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    int pos = rand() % LED_STRIP_COUNT;
    led_strip_set_pixel(s_led_strip, pos, 0, 255, 40); // Bright Matrix Green
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Cockpit police lights: left red, right blue alternating
void led_effects_police(bool toggle) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(false);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    int half = LED_STRIP_COUNT / 2;
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        if (toggle) {
            if (i < half) led_strip_set_pixel(s_led_strip, i, 220, 0, 0); // Red
            else led_strip_set_pixel(s_led_strip, i, 0, 0, 220);          // Blue
        } else {
            if (i < half) led_strip_set_pixel(s_led_strip, i, 0, 0, 220); // Blue
            else led_strip_set_pixel(s_led_strip, i, 220, 0, 0);          // Red
        }
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Fire truck water cannon spray: rushing cyan/blue water wave
void led_effects_water_spray(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        uint8_t b = 180 + (rand() % 75);
        uint8_t g = 120 + (rand() % 100);
        led_strip_set_pixel(s_led_strip, i, 0, g, b);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Fire truck turn signal: amber blink on left or right half
void led_effects_turn_signal(bool left) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    led_strip_clear(s_led_strip);
    int half = LED_STRIP_COUNT / 2;
    int start = left ? 0 : half;
    int end = left ? half : LED_STRIP_COUNT;
    for (int i = start; i < end; i++) {
        led_strip_set_pixel(s_led_strip, i, 255, 130, 0); // Amber
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Fire truck searchlight: blazing pure white beam
void led_effects_searchlight(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        led_strip_set_pixel(s_led_strip, i, 255, 255, 255);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Fire glow: flickering warm red-orange embers
void led_effects_fire_glow(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        uint8_t r = 200 + (rand() % 55);
        uint8_t g = 30 + (rand() % 70);
        led_strip_set_pixel(s_led_strip, i, r, g, 0);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Rescue success: celebration rainbow wave
void led_effects_rescue_success(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        uint8_t r, g, b;
        hsv_to_rgb((i * 360 / LED_STRIP_COUNT + 120) % 360, 100, 100, &r, &g, &b);
        led_strip_set_pixel(s_led_strip, i, r, g, b);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Gentle breathing animation when idle
void led_effects_idle_step(void) {
    if (!s_led_strip || !s_led_mutex) return;
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_idle_step++;
    float rad = (float)s_idle_step * 0.08f;
    float brightness = (sinf(rad) + 1.0f) * 0.5f * 28.0f + 4.0f; // Soft 4% to 32% breathing

    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        uint8_t r, g, b;
        hsv_to_rgb((s_idle_step * 2 + i * 6) % 360, 80, (uint8_t)brightness, &r, &g, &b);
        led_strip_set_pixel(s_led_strip, i, r, g, b);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Light up exactly N LEDs for counting
void led_effects_count(uint8_t count, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    s_idle_elapsed_ms = 0;
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    led_strip_clear(s_led_strip);
    if (count > LED_STRIP_COUNT) count = LED_STRIP_COUNT;

    for (int i = 0; i < count; i++) {
        led_strip_set_pixel(s_led_strip, i, r, g, b);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Volume bar indicator on LED strip
void led_effects_volume(float volume) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    led_strip_clear(s_led_strip);
    int active_leds = (int)(volume * LED_STRIP_COUNT + 0.5f);
    if (active_leds > LED_STRIP_COUNT) active_leds = LED_STRIP_COUNT;

    for (int i = 0; i < active_leds; i++) {
        float ratio = (float)i / (float)LED_STRIP_COUNT;
        uint8_t r = 0, g = 0, b = 0;
        if (ratio < 0.60f) {
            r = 0; g = 180; b = 80;   // Green
        } else if (ratio < 0.85f) {
            r = 200; g = 180; b = 0;  // Yellow
        } else {
            r = 240; g = 30; b = 30;  // Red
        }
        led_strip_set_pixel(s_led_strip, i, r, g, b);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Mozart pitch visualizer: maps musical pitch (C3..C6) across the LED strip with timbre colors
void led_effects_mozart_note(float freq, float bass_freq, uint8_t timbre) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    led_strip_clear(s_led_strip);

    // Map 130 Hz (C3) - 1050 Hz (C6) to 0..(LED_STRIP_COUNT - 1)
    float ratio = 0.5f;
    if (freq >= 130.0f) {
        ratio = log2f(freq / 130.0f) / 3.0f; // 3 octaves
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
    }
    int center = (int)(ratio * (LED_STRIP_COUNT - 1) + 0.5f);
    if (center >= LED_STRIP_COUNT) center = LED_STRIP_COUNT - 1;

    // Pick base color according to timbre & register
    uint8_t mr, mg, mb;
    if (timbre == 1) { // Music Box: Crystal Ice / Lavender
        mr = (uint8_t)(100 + ratio * 155.0f);
        mg = (uint8_t)(200 + (1.0f - ratio) * 55.0f);
        mb = 255;
    } else if (timbre == 2) { // 8-Bit Marimba: Bright Arcade Neon
        uint16_t hue = (uint16_t)(ratio * 300.0f);
        hsv_to_rgb(hue, 100, 90, &mr, &mg, &mb);
    } else { // Grand Piano & others: Golden Amber to Brilliant Azure
        uint16_t hue = (uint16_t)(35.0f + ratio * 200.0f);
        hsv_to_rgb(hue, 85, 95, &mr, &mg, &mb);
    }

    // Light center note and soft glow neighbors
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        int dist = abs(i - center);
        if (dist == 0) {
            led_strip_set_pixel(s_led_strip, i, mr, mg, mb);
        } else if (dist == 1) {
            led_strip_set_pixel(s_led_strip, i, mr / 3, mg / 3, mb / 3);
        } else if (dist == 2) {
            led_strip_set_pixel(s_led_strip, i, mr / 8, mg / 8, mb / 8);
        }
    }

    // If harmonic bass accompaniment note was played, light up the bass end (LED 0-2) in warm amber
    if (bass_freq > 20.0f) {
        led_strip_set_pixel(s_led_strip, 0, 220, 90, 10);
        led_strip_set_pixel(s_led_strip, 1, 160, 60, 5);
    }

    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// ============================================================================
// MODE 3: SPEED RUNNER (TACHOMETER & RACING VISUAL EFFECTS)
// ============================================================================

// Speedometer visualizer: Displays racing speed across 30 LEDs like a dynamic car tachometer
void led_effects_speedometer(uint8_t speed_level, float speed_ratio) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    static uint16_t s_speed_step = 0;
    s_speed_step = (s_speed_step + 1) % 360;

    led_strip_clear(s_led_strip);

    if (speed_level == 0) {
        // Xe dung: 2 bong LED do nhe o 2 dau duoi xe (Tail lights)
        led_strip_set_pixel(s_led_strip, 0, 40, 0, 0);
        led_strip_set_pixel(s_led_strip, 1, 15, 0, 0);
        led_strip_set_pixel(s_led_strip, LED_STRIP_COUNT - 2, 15, 0, 0);
        led_strip_set_pixel(s_led_strip, LED_STRIP_COUNT - 1, 40, 0, 0);
    } else if (speed_level >= 4) {
        // CAP 4 (NITRO TURBO BOOST): Dai cau vong cuon song sieu toc ruc ro
        for (int i = 0; i < LED_STRIP_COUNT; i++) {
            uint8_t r, g, b;
            uint16_t hue = (s_speed_step * 20 + i * 12) % 360;
            hsv_to_rgb(hue, 100, 95, &r, &g, &b);
            led_strip_set_pixel(s_led_strip, i, r, g, b);
        }
    } else {
        // CAP 1..3: Thanh do toc do leo dan tu trai qua phai (Tachometer bar)
        if (speed_ratio < 0.05f) speed_ratio = 0.05f;
        if (speed_ratio > 1.0f) speed_ratio = 1.0f;
        int active_leds = (int)(speed_ratio * (float)LED_STRIP_COUNT + 0.5f);
        if (active_leds < 2) active_leds = 2;
        if (active_leds > LED_STRIP_COUNT) active_leds = LED_STRIP_COUNT;

        for (int i = 0; i < active_leds; i++) {
            float pos = (float)i / (float)LED_STRIP_COUNT;
            uint8_t r = 0, g = 0, b = 0;
            if (pos < 0.40f) {
                r = 0; g = 190; b = 40;   // Green Cruising
            } else if (pos < 0.75f) {
                r = 250; g = 160; b = 0;  // Amber Sport
            } else {
                r = 255; g = 20; b = 10;  // Racing Red
            }

            if (i == active_leds - 1) {
                r = 255; g = 255; b = 255;
            }
            led_strip_set_pixel(s_led_strip, i, r, g, b);
        }
    }

    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

void led_effects_speed_brake(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        led_strip_set_pixel(s_led_strip, i, 255, 0, 0);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

void led_effects_speed_nitro(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;

    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        led_strip_set_pixel(s_led_strip, i, 120, 240, 255);
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}
