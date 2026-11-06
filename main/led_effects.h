#ifndef LED_EFFECTS_H
#define LED_EFFECTS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define PIN_LED_STRIP     13   // WS2812B Data In GPIO (Gắn chân 13)
#define LED_STRIP_COUNT   32   // Số lượng bóng LED WS2812B (32 LED)

typedef enum {
    LED_STYLE_REACTIVE = 0, // Nháy theo phím bấm & âm nhạc (mặc định)
    LED_STYLE_RAINBOW,      // Sóng cầu vồng chuyển động liên tục
    LED_STYLE_BREATHING,    // Đèn ngủ đổi màu thở êm dịu
    LED_STYLE_STARRY,       // Sao băng lấp lánh (sparkle/fireflies)
    LED_STYLE_OFF,          // Tắt hẳn dải đèn (Night mode / tiết kiệm năng lượng)
    LED_STYLE_TOTAL
} led_style_t;

esp_err_t led_effects_init(void);
void led_effects_clear(void);
void led_effects_tick(uint32_t delta_ms);

// Chuyển đổi hiệu ứng đèn LED cho BTN 4
led_style_t led_effects_next_style(void);
const char *led_effects_get_style_name(led_style_t style);
led_style_t led_effects_get_current_style(void);

// Visual FX for All Modes
void led_effects_mode_switch(uint8_t mode_index);
void led_effects_note(uint8_t note_idx, uint8_t total_notes, uint8_t r, uint8_t g, uint8_t b);
void led_effects_beat(void);
void led_effects_spark(void);
void led_effects_color_burst(uint8_t r, uint8_t g, uint8_t b);
void led_effects_matrix(void);
void led_effects_police(bool toggle);
void led_effects_water_spray(void);
void led_effects_turn_signal(bool left);
void led_effects_searchlight(void);
void led_effects_fire_glow(void);
void led_effects_rescue_success(void);
void led_effects_idle_step(void);
void led_effects_count(uint8_t count, uint8_t r, uint8_t g, uint8_t b);
void led_effects_start_counting(uint8_t target_count, uint32_t delay_ms, uint32_t step_ms);
void led_effects_carriage(uint8_t col, uint8_t max_col);
void led_effects_volume(float volume);
void led_effects_mozart_note(float freq, float bass_freq, uint8_t timbre);
void led_effects_celebration(void);
void led_effects_speedometer(uint8_t speed_level, float speed_ratio);
void led_effects_speed_brake(void);
void led_effects_speed_nitro(void);

#endif // LED_EFFECTS_H
