#ifndef LED_EFFECTS_H
#define LED_EFFECTS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define PIN_LED_STRIP          13   // WS2812B Data In GPIO (Gắn chân 13)
#define LED_STRIP_COUNT        32   // Khung Hình Vuông 4 cạnh x 8 LED = 32 LED
#define LED_SQUARE_SIDE_LEN     8   // Số bóng LED trên mỗi cạnh hình vuông (8 LED)
#define LED_SQUARE_SIDES        4   // 4 cạnh hình vuông (0: Trên, 1: Phải, 2: Dưới, 3: Trái)

typedef enum {
    LED_STYLE_REACTIVE = 0, // Nháy tương tác 40 FPS theo phím bấm & âm nhạc (mặc định)
    LED_STYLE_RAINBOW,      // Sóng cầu vồng xoay 360° quanh khung hình vuông
    LED_STYLE_BREATHING,    // Đèn ngủ đổi màu thở đối xứng 4 cạnh êm dịu
    LED_STYLE_STARRY,       // Sao băng & đom đóm lấp lánh 4 góc vuông
    LED_STYLE_OFF,          // Tắt hẳn dải đèn (Night mode / tiết kiệm năng lượng)
    LED_STYLE_TOTAL
} led_style_t;

// Các hoạt cảnh đồ họa chuyên biệt trên Khung Vuông 4x8 cho Chế độ 6 (Cartoon & Game)
typedef enum {
    CARTOON_ANIM_COIN_POP = 0,     // Đồng xu vàng bắn vọt từ cạnh dưới lên cạnh trên
    CARTOON_ANIM_SPRING_BOUNCE,    // Lò xo bật nảy qua lại giữa 4 cạnh hình vuông
    CARTOON_ANIM_POWERUP_GROW,     // Nấm Power-Up nở dần từ 1 cạnh ra toàn bộ 4 cạnh
    CARTOON_ANIM_1UP_PULSE,        // 3 nhịp sóng xanh ngọc lục bảo từ 4 tâm cạnh
    CARTOON_ANIM_PROJECTILE,       // Đạn lửa / Tia Laser bắn dọc 2 cạnh hội tụ nổ ở cạnh đối diện
    CARTOON_ANIM_WARP_PORTAL,      // Cổng không gian xoáy 4 cạnh hút vào 4 góc vuông
    CARTOON_ANIM_BOMB_EXPLODE,     // Bom nổ chớp trắng 4 góc -> sóng lửa đỏ cam tràn 4 cạnh
    CARTOON_ANIM_PACMAN,           // Pac-Man vàng chạy vòng quanh 4 cạnh ăn chấm trắng
    CARTOON_ANIM_SONIC_SPIN,       // Vòng xoáy siêu thanh Sonic quay tít quanh khung vuông
    CARTOON_ANIM_SLIP_FALL,        // Trượt vỏ chuối tuột dọc cạnh khung vuông rồi chớp đỏ
    CARTOON_ANIM_DIZZY_STARS,      // 4 ngôi sao choáng váng quay tròn quanh 4 cạnh hình vuông
    CARTOON_ANIM_MAGIC_WAND,       // Đũa thần quét qua 4 cạnh rải kim tuyến tím-hồng-trắng
    CARTOON_ANIM_TOM_JERRY_CHASE,  // Mèo Tom (Xanh) & Chuột Jerry (Vàng) rượt đuổi quanh khung vuông
    CARTOON_ANIM_PUNCH_IMPACT      // Cú đấm K.O. chấn động 2 cạnh đối diện
} cartoon_led_anim_t;

esp_err_t led_effects_init(void);
void led_effects_clear(void);
void led_effects_tick(uint32_t delta_ms);

// Điều chỉnh độ sáng tổng và quản lý năng lượng LED
void led_effects_set_master_brightness(uint8_t brightness);
uint8_t led_effects_get_master_brightness(void);
void led_effects_reset_idle(void);

// Chuyển đổi hiệu ứng đèn LED cho BTN 4
led_style_t led_effects_next_style(void);
const char *led_effects_get_style_name(led_style_t style);
led_style_t led_effects_get_current_style(void);

// Hiệu ứng hệ thống & dùng chung (Đã nâng cấp lên Engine 40 FPS trên Khung Vuông 4x8)
void led_effects_mode_switch(uint8_t mode_index);
void led_effects_startup(void);
void led_effects_standby(bool active);
void led_effects_note(uint8_t note_idx, uint8_t total_notes, uint8_t r, uint8_t g, uint8_t b);
void led_effects_beat(void);
void led_effects_music_energy(float energy);
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

// ============================================================================
// CÁC HIỆU ỨNG CHUYÊN BIỆT CHO KHUNG HÌNH VUÔNG 4 CẠNH x 8 LED (SQUARE 4x8)
// ============================================================================

// Tiện ích hình học Khung Vuông: Thắp sáng & phóng sóng từ 1 trong 4 cạnh (0=Trên, 1=Phải, 2=Dưới, 3=Trái)
void led_effects_square_side_pulse(uint8_t side_idx, uint8_t r, uint8_t g, uint8_t b);
void led_effects_square_spin(bool clockwise, uint8_t r, uint8_t g, uint8_t b);
// Quét hướng: bắt đầu từ LED trung tâm cạnh `side` (0=Trên, 1=Phải, 2=Dưới, 3=Trái), chia 2 nhánh chạy vòng
// qua 2 bên và sáng dần cho tới LED trung tâm của cạnh đối diện.
void led_effects_direction_sweep(uint8_t side, uint8_t r, uint8_t g, uint8_t b);

// Mode 1: Đàn Piano Diệu Kỳ & Ngũ Cung (4 Hàng phím = 4 Cạnh hình vuông)
void led_effects_piano_key(uint8_t row_side, uint8_t pos_in_side, uint8_t timbre, uint8_t r, uint8_t g, uint8_t b);
void led_effects_piano_chord(uint8_t r, uint8_t g, uint8_t b);
void led_effects_pentatonic_toggle(bool enabled);

// Mode 2: Baby Mozart (Sao chổi giao hưởng + 4 góc Bass + Vòng tiến độ 32 LED)
void led_effects_mozart_progress(float freq, float bass_freq, uint8_t timbre, uint16_t note_idx, uint16_t total_notes, bool autoplay);

// Mode 4: Dàn Trống Beat-Maker & DJ 4 Cạnh / 4 Góc
void led_effects_drum_kick(int8_t semi);
void led_effects_drum_snare(int8_t semi);
void led_effects_drum_hihat(int8_t semi);
void led_effects_drum_tom(uint8_t tom_idx);
void led_effects_drum_cymbal(int8_t semi);
void led_effects_drum_clap(void);
void led_effects_drum_dj_beat(uint8_t beat_step, bool active);

// Mode 5: Máy Gõ Chữ Cổ Điển (4 dòng x 8 cột trên khung vuông)
void led_effects_typewriter_key(uint8_t col, uint8_t max_col, bool is_space);
void led_effects_typewriter_backspace(uint8_t col, uint8_t max_col);
void led_effects_typewriter_return(uint8_t from_col);

// Mode 6: Hoạt Hình & Game SFX (14 hoạt cảnh Pixel 1D trên khung vuông)
void led_effects_cartoon_fx(cartoon_led_anim_t anim, uint8_t r, uint8_t g, uint8_t b);

// Mode 7: Điệp Viên Nhí & Robot R2-D2 (Mã Morse quang học + Radar 360° + R2-D2 Holo)
void led_effects_morse_code(const char *morse_str);
void led_effects_radar_sweep(void);
void led_effects_r2d2_holo(void);

// Mode 8: Buồng Lái Xe Cứu Hỏa 114 & 26 Con Vật
void led_effects_animal_pulse(uint8_t animal_idx, uint8_t r, uint8_t g, uint8_t b);
void led_effects_cockpit_accel(void);
void led_effects_cockpit_reverse(void);
void led_effects_helicopter_rotor(void);
void led_effects_radio_wave(void);
void led_effects_bell_ring(void);

// Mode 9: Bé Học Đánh Vần & Tập Đếm Số
void led_effects_phonics_letter(char upper_letter);
void led_effects_math_quiz_display(uint8_t num_a, uint8_t op, uint8_t num_b, int8_t ans, bool pulse_guide);

#endif // LED_EFFECTS_H
