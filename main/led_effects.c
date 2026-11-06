#include "led_effects.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "LED_FX_SQ32";

// Phép bọc vòng khép kín 32 LED (0..31 nối tiếp thành Khung Hình Vuông 4 cạnh x 8 LED)
#define WRAP32(x) (((int)(x)) & 0x1F)

static led_strip_handle_t s_led_strip = NULL;
static SemaphoreHandle_t s_led_mutex = NULL;

// Bộ đệm khung hình 32 LED (RGB Framebuffer có hiệu ứng tắt dần mượt mà 40 FPS)
static uint8_t s_canvas_r[LED_STRIP_COUNT];
static uint8_t s_canvas_g[LED_STRIP_COUNT];
static uint8_t s_canvas_b[LED_STRIP_COUNT];
static bool    s_canvas_active = false;
static uint8_t s_decay_rate = 205; // Tốc độ mờ dần mặc định mỗi 25ms (205/256 ~ 80%)

// Hệ thống Sóng Lan Tỏa Đa Âm (Polyphonic Ripples chạy vòng quanh 4 cạnh hình vuông)
#define MAX_RIPPLES 8
typedef struct {
    bool     active;
    uint8_t  center;      // Tâm xuất phát (0..31)
    uint8_t  step;        // Bước thời gian hiện tại
    uint8_t  max_steps;   // Tổng số bước lan tỏa
    uint8_t  r, g, b;     // Màu sóng
    uint8_t  style;       // 0: Sóng mượt, 1: 8-bit khối vuông, 2: Kim tuyến Music Box, 3: Phản xạ cạnh đối diện
} sq_ripple_t;

static sq_ripple_t s_ripples[MAX_RIPPLES];

// Các chế độ Hoạt Cảnh Chuyên Biệt trên Khung Hình Vuông 4x8
typedef enum {
    LED_ANIM_NONE = 0,
    LED_ANIM_MODE_SWITCH,        // Xoay 4 tia cầu vồng quanh 4 cạnh khi chuyển chế độ
    LED_ANIM_CELEBRATION,        // Pháo hoa 4 góc & cầu vồng xoay quanh hình vuông
    LED_ANIM_COUNTING,           // Đoàn tàu đếm số cầu vồng + chớp 4 góc khen thưởng
    LED_ANIM_SQUARE_SPIN,        // Xoay 1 vòng 360° thuận/ngược chiều kim đồng hồ
    LED_ANIM_DIRECTION_SWEEP,    // Quét hướng: từ tâm cạnh bấm sáng dần 2 nhánh tới tâm cạnh đối diện
    LED_ANIM_TURN_SIGNAL,        // Đèn xi-nhan chạy đuổi tuần tự kiểu Audi ôm góc vuông
    LED_ANIM_WATER_CANNON,       // 2 luồng nước phun dọc 2 cạnh bên dập lửa ở cạnh đối diện
    LED_ANIM_FIRE_FLICKER,       // Ngọn lửa cháy bập bùng động trên 4 cạnh hình vuông
    LED_ANIM_SEARCHLIGHT,        // Đèn pha trắng quét vòng 360° quanh 4 cạnh
    LED_ANIM_HELICOPTER,         // Cánh quạt trực thăng 4 cánh (0, 8, 16, 24) quay tít
    LED_ANIM_RADAR_SONAR,        // Tia Radar xanh lục quét 360° + nháy đỏ mục tiêu
    LED_ANIM_MORSE_STREAM,       // Chuỗi Chấm-Gạch mã Morse chạy quanh 4 cạnh khung vuông
    LED_ANIM_R2D2_HOLO,          // 8 ô dữ liệu R2-D2 đảo trạng thái liên tục trên 4 cạnh
    LED_ANIM_TYPEWRITER_RETURN,  // Con trỏ máy chữ trượt vèo ngược về đầu trang (LED 0)
    LED_ANIM_CARTOON_FX,         // 14 hoạt cảnh đồ họa Pixel 1D trên khung vuông
    LED_ANIM_DRUM_CYMBAL_SHIMMER,// Chũm chọe Cymbal ngân rung lấp lánh 4 cạnh
    LED_ANIM_RADIO_WAVE,         // Sóng âm bộ đàm 114 nhấp nhô từ 4 tâm cạnh
    LED_ANIM_BELL_RING,          // Chuông báo cháy đập quả lắc qua lại giữa các cạnh
    LED_ANIM_REVERSE_BEEP,       // Đèn số lùi nhấp nháy ở cạnh dưới & 4 góc
    LED_ANIM_ACCEL_BOOST         // Luồng lửa tăng tốc phóng dọc 2 cạnh bên
} led_anim_type_t;

static volatile led_anim_type_t s_anim_type = LED_ANIM_NONE;
static volatile uint16_t s_anim_step = 0;
static volatile uint16_t s_anim_hue = 0;
static volatile uint8_t  s_anim_sub = 0;
static volatile uint8_t  s_anim_r = 255, s_anim_g = 200, s_anim_b = 0;

// Biến đếm số (Mode 9)
static volatile uint8_t  s_count_target = 0;
static volatile uint8_t  s_count_current = 0;
static volatile int32_t  s_count_wait_ms = 0;
static volatile uint32_t s_count_step_ms = 200;

// Biến Mã Morse (Mode 7)
static char s_morse_buf[16] = {0};

// Biến Nhịp nền DJ 4 cạnh (Mode 4)
static bool    s_dj_beat_active = false;
static uint8_t s_dj_beat_step = 0;

// Biến Đường đua F1 Hình Vuông (Mode 3)
static float   s_race_car_pos = 0.0f;
static float   s_race_car_speed = 0.0f;
static uint8_t s_race_speed_level = 0;
static uint32_t s_race_active_ms = 0;

// Biến Tiến độ Mozart & 4 góc Bass (Mode 2)
static uint8_t s_mozart_bass_glow = 0;
static uint8_t s_mozart_progress_leds = 0;
static bool    s_mozart_autoplay = false;
static uint16_t s_mozart_aurora_step = 0;

// Biến Trạng thái trang giấy Máy Gõ Chữ (Mode 5)
static bool    s_tw_page_active = false;
static uint8_t s_tw_col = 0;
static uint8_t s_tw_max_col = 32;
static uint8_t s_tw_head_flash = 0;

static uint32_t s_idle_step = 0;
static uint32_t s_idle_elapsed_ms = 0;
static led_style_t s_current_style = LED_STYLE_REACTIVE;
static uint16_t s_style_step = 0;

static const char *s_style_names[LED_STYLE_TOTAL] = {
    [LED_STYLE_REACTIVE]  = "TUONG TAC KHUNG VUONG 4x8 (40 FPS)",
    [LED_STYLE_RAINBOW]   = "CAU VONG XOAY 360 QUANH HINH VUONG",
    [LED_STYLE_BREATHING] = "DEN NGU DOI XUNG 4 CANH EM DIU",
    [LED_STYLE_STARRY]    = "SAO BANG & DOM DOM 4 GOC VUONG",
    [LED_STYLE_OFF]       = "TAT DEN LED (NIGHT MODE)"
};

// Tâm của 4 cạnh hình vuông (Mỗi cạnh 8 LED: 0..7, 8..15, 16..23, 24..31)
static const uint8_t s_side_centers[4] = { 3, 11, 19, 27 };
// 4 góc của hình vuông
static const uint8_t s_corners[4]      = { 0, 8, 16, 24 };

// ============================================================================
// HÀM TIỆN ÍCH NỘI BỘ (MÀU SẮC & KHUNG HÌNH)
// ============================================================================

static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    float hf = (float)(h % 360) / 60.0f;
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

// Cộng dồn ánh sáng vào 1 điểm trên khung vuông (tự động bọc vòng 0..31 và chống tràn 255)
static inline void canvas_add_pixel(int idx, int r, int g, int b) {
    int p = WRAP32(idx);
    int nr = (int)s_canvas_r[p] + r;
    int ng = (int)s_canvas_g[p] + g;
    int nb = (int)s_canvas_b[p] + b;
    s_canvas_r[p] = (uint8_t)(nr > 255 ? 255 : (nr < 0 ? 0 : nr));
    s_canvas_g[p] = (uint8_t)(ng > 255 ? 255 : (ng < 0 ? 0 : ng));
    s_canvas_b[p] = (uint8_t)(nb > 255 ? 255 : (nb < 0 ? 0 : nb));
    s_canvas_active = true;
}

static inline void canvas_set_pixel(int idx, uint8_t r, uint8_t g, uint8_t b) {
    int p = WRAP32(idx);
    s_canvas_r[p] = r;
    s_canvas_g[p] = g;
    s_canvas_b[p] = b;
    s_canvas_active = true;
}

static inline void canvas_clear_mem(void) {
    memset(s_canvas_r, 0, sizeof(s_canvas_r));
    memset(s_canvas_g, 0, sizeof(s_canvas_g));
    memset(s_canvas_b, 0, sizeof(s_canvas_b));
}

// Thắp sáng 4 góc hình vuông
static void canvas_add_corners(uint8_t r, uint8_t g, uint8_t b) {
    for (int c = 0; c < 4; c++) {
        int corner = s_corners[c];
        canvas_add_pixel(corner - 1, r / 2, g / 2, b / 2);
        canvas_add_pixel(corner,     r,     g,     b);
    }
}

// Thắp sáng trọn vẹn 1 cạnh của hình vuông (side 0..3, mỗi cạnh 8 LED)
static void canvas_add_side(uint8_t side, uint8_t r, uint8_t g, uint8_t b) {
    int base = (side & 0x03) * LED_SQUARE_SIDE_LEN;
    for (int i = 0; i < LED_SQUARE_SIDE_LEN; i++) {
        // Sáng rực ở tâm cạnh và dịu nhẹ ở 2 đầu góc
        bool is_center = (i >= 2 && i <= 5);
        canvas_add_pixel(base + i, is_center ? r : (r * 2) / 3,
                                   is_center ? g : (g * 2) / 3,
                                   is_center ? b : (b * 2) / 3);
    }
}

// Đẩy dữ liệu từ Canvas ra dải LED phần cứng (Yêu cầu đã giữ s_led_mutex)
static void flush_canvas_locked(void) {
    for (int i = 0; i < LED_STRIP_COUNT; i++) {
        led_strip_set_pixel(s_led_strip, i, s_canvas_r[i], s_canvas_g[i], s_canvas_b[i]);
    }
    led_strip_refresh(s_led_strip);
}

// Khởi tạo 1 sóng lan tỏa (Ripple) trên khung vuông 32 LED
static void spawn_square_ripple(uint8_t center, uint8_t max_steps, uint8_t r, uint8_t g, uint8_t b, uint8_t style) {
    int slot = -1;
    uint8_t oldest_step = 0;
    int oldest_idx = 0;
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (!s_ripples[i].active) {
            slot = i;
            break;
        }
        if (s_ripples[i].step >= oldest_step) {
            oldest_step = s_ripples[i].step;
            oldest_idx = i;
        }
    }
    if (slot < 0) slot = oldest_idx;

    s_ripples[slot].active    = true;
    s_ripples[slot].center    = WRAP32(center);
    s_ripples[slot].step      = 0;
    s_ripples[slot].max_steps = (max_steps > 0) ? max_steps : 10;
    s_ripples[slot].r         = r;
    s_ripples[slot].g         = g;
    s_ripples[slot].b         = b;
    s_ripples[slot].style     = style;

    // Vẽ ngay điểm bùng nổ đầu tiên để độ trễ hiển thị = 0ms
    canvas_add_pixel(center, r, g, b);
    canvas_add_pixel(center - 1, r / 2, g / 2, b / 2);
    canvas_add_pixel(center + 1, r / 2, g / 2, b / 2);
    if (style == 3) {
        // Phản xạ tia chớp sang cạnh đối diện của hình vuông
        int opp = WRAP32(center + 16);
        canvas_add_pixel(opp, r / 2, g / 2, b / 2);
    }
}

static inline void touch_led_activity(bool cancel_anim) {
    if (s_current_style == LED_STYLE_OFF) return;
    if (cancel_anim && s_anim_type != LED_ANIM_CELEBRATION) {
        s_anim_type = LED_ANIM_NONE;
    }
    // Tắt hiển thị tĩnh của máy chữ / đua xe nếu chuyển sang hiệu ứng khác
    s_tw_page_active = false;
    s_idle_elapsed_ms = 0;
    s_canvas_active = true;
}

// ============================================================================
// QUẢN LÝ STYLE & KHỞI TẠO PHẦN CỨNG
// ============================================================================

const char *led_effects_get_style_name(led_style_t style) {
    if (style < LED_STYLE_TOTAL && s_style_names[style]) {
        return s_style_names[style];
    }
    return "UNKNOWN";
}

led_style_t led_effects_get_current_style(void) {
    return s_current_style;
}

led_style_t led_effects_next_style(void) {
    s_current_style = (led_style_t)((s_current_style + 1) % LED_STYLE_TOTAL);
    if (s_current_style == LED_STYLE_OFF) {
        led_effects_clear();
    } else if (s_current_style == LED_STYLE_RAINBOW) {
        led_effects_celebration();
    } else if (s_current_style == LED_STYLE_BREATHING) {
        led_effects_piano_chord(0, 180, 255);
    } else if (s_current_style == LED_STYLE_STARRY) {
        led_effects_spark();
    } else {
        // Quay lại style mặc định: xoay 1 vòng báo hiệu (không reset trạng thái của chế độ đang chạy)
        led_effects_square_spin(true, 255, 255, 255);
    }
    return s_current_style;
}

esp_err_t led_effects_init(void) {
    ESP_LOGI(TAG, "Initializing 4x8 Square WS2812B LED Frame on GPIO %d (%d LEDs)", PIN_LED_STRIP, LED_STRIP_COUNT);

    s_led_mutex = xSemaphoreCreateMutex();
    canvas_clear_mem();
    memset(s_ripples, 0, sizeof(s_ripples));

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
    led_effects_mode_switch(0);
    ESP_LOGI(TAG, "WS2812B 4x8 Square Engine (40 FPS) initialized!");
    return ESP_OK;
}

void led_effects_clear(void) {
    if (!s_led_strip || !s_led_mutex) return;
    s_anim_type = LED_ANIM_NONE;
    s_tw_page_active = false;
    s_dj_beat_active = false;
    s_mozart_autoplay = false;
    s_mozart_progress_leds = 0;
    s_race_active_ms = 0;
    memset(s_ripples, 0, sizeof(s_ripples));
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    canvas_clear_mem();
    s_canvas_active = false;
    led_strip_clear(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// ============================================================================
// BỘ XỬ LÝ HOẠT CẢNH CHUYÊN BIỆT TRÊN KHUNG HÌNH VUÔNG (CHẠY Ở 40 FPS)
// ============================================================================

static void render_cartoon_anim_step(cartoon_led_anim_t anim, uint16_t step, uint8_t r, uint8_t g, uint8_t b) {
    switch (anim) {
        case CARTOON_ANIM_COIN_POP: {
            // Đồng xu vàng bắn từ tâm cạnh dưới (19-20) chạy vòng 2 bên lên đỉnh cạnh trên (3-4)
            if (step < 14) {
                int offset = step;
                canvas_add_pixel(19 - offset, 255, 220, 0);
                canvas_add_pixel(20 + offset, 255, 220, 0);
                if (step >= 8) {
                    // Tới đỉnh nổ kim tuyến vàng trắng ở cạnh trên & 4 góc
                    canvas_add_side(0, 255, 215, 40);
                    canvas_add_corners(255, 255, 180);
                }
            }
            break;
        }
        case CARTOON_ANIM_SPRING_BOUNCE: {
            // Lò xo bật nảy qua lại giữa 4 cạnh hình vuông theo hàm sin đàn hồi
            if (step < 18) {
                float phase = (float)step * 0.55f;
                int bounce_pos = (int)(sinf(phase) * 12.0f) + 16;
                for (int w = -1; w <= 1; w++) {
                    canvas_add_pixel(bounce_pos + w, r, g, b);
                    canvas_add_pixel(bounce_pos + 16 + w, g, r, b);
                }
            }
            break;
        }
        case CARTOON_ANIM_POWERUP_GROW: {
            // Nấm biến hình khổng lồ: Sáng dần từ 1 cạnh -> 2 cạnh -> 3 cạnh -> cả 4 cạnh cầu vồng!
            if (step < 16) {
                int active_sides = (step / 4) + 1;
                for (int s = 0; s < active_sides && s < 4; s++) {
                    uint8_t cr, cg, cb;
                    hsv_to_rgb((step * 25 + s * 90) % 360, 100, 95, &cr, &cg, &cb);
                    canvas_add_side(s, cr, cg, cb);
                }
            }
            break;
        }
        case CARTOON_ANIM_1UP_PULSE: {
            // 3 nhịp sóng Xanh Ngọc Lục Bảo tỏa từ 4 tâm cạnh ra 4 góc
            if (step < 15) {
                int dist = step % 5;
                for (int s = 0; s < 4; s++) {
                    int c = s_side_centers[s];
                    canvas_add_pixel(c - dist, 0, 255, 160);
                    canvas_add_pixel(c + 1 + dist, 0, 255, 160);
                }
            }
            break;
        }
        case CARTOON_ANIM_PROJECTILE: {
            // 2 viên đạn lửa / tia Laser bắn dọc 2 cạnh từ đỉnh (3-4) hội tụ nổ tung ở đáy (19-20)
            if (step < 14) {
                int d = step * 2;
                if (d <= 16) {
                    canvas_add_pixel(3 - d, 255, 255, 200);
                    canvas_add_pixel(3 - d + 1, r, g, b);
                    canvas_add_pixel(4 + d, 255, 255, 200);
                    canvas_add_pixel(4 + d - 1, r, g, b);
                } else {
                    // Hội tụ phát nổ ở cạnh đối diện
                    canvas_add_side(2, 255, 120, 0);
                    canvas_add_corners(r, g, b);
                }
            }
            break;
        }
        case CARTOON_ANIM_WARP_PORTAL: {
            // Cổng không gian hình vuông: 4 tia Xanh-Tím xoáy vào 4 góc
            if (step < 16) {
                for (int s = 0; s < 4; s++) {
                    int p1 = s * 8 + (step % 8);
                    int p2 = s * 8 + 7 - (step % 8);
                    canvas_add_pixel(p1, 0, 180, 255);
                    canvas_add_pixel(p2, 200, 0, 255);
                }
            }
            break;
        }
        case CARTOON_ANIM_BOMB_EXPLODE: {
            // Bom nổ: Chớp trắng toàn khung -> Sóng lửa đỏ-cam-vàng nổ từ 4 góc -> Khói tàn đỏ
            if (step < 3) {
                for (int i = 0; i < LED_STRIP_COUNT; i++) canvas_set_pixel(i, 255, 255, 255);
            } else if (step < 16) {
                for (int i = 0; i < LED_STRIP_COUNT; i++) {
                    if ((rand() % 10) > (step - 3)) {
                        canvas_add_pixel(i, 255, (rand() % 140), 0);
                    }
                }
            }
            break;
        }
        case CARTOON_ANIM_PACMAN: {
            // Pac-Man vàng chạy vòng quanh 4 cạnh ăn từng hạt chấm trắng
            if (step < 20) {
                int pac_pos = (step * 2) & 0x1F;
                // Vẽ các hạt chấm trắng chưa bị ăn phía trước
                for (int i = pac_pos + 3; i < LED_STRIP_COUNT; i += 3) {
                    canvas_set_pixel(i, 60, 60, 80);
                }
                // Vẽ Pac-Man vàng rực (3 LED)
                canvas_set_pixel(pac_pos,     255, 230, 0);
                canvas_set_pixel(pac_pos - 1, 255, 180, 0);
                canvas_set_pixel(pac_pos + 1, (step & 1) ? 255 : 0, (step & 1) ? 230 : 0, 0); // Miệng nhai Waka-Waka
            }
            break;
        }
        case CARTOON_ANIM_SONIC_SPIN: {
            // Sonic xoay tít siêu thanh 3 vòng quanh khung hình vuông
            if (step < 18) {
                int pos = (step * 5) & 0x1F;
                for (int t = 0; t < 6; t++) {
                    uint8_t intensity = 255 - t * 40;
                    canvas_add_pixel(pos - t, 0, intensity / 2, intensity);
                    canvas_add_pixel(pos + 16 - t, 0, intensity / 2, intensity);
                }
            }
            break;
        }
        case CARTOON_ANIM_SLIP_FALL: {
            // Trượt vỏ chuối: Ánh sáng trượt tuột từ cạnh trên xuống cạnh dưới rồi chớp đỏ
            if (step < 10) {
                int pos = step * 2;
                canvas_add_pixel(pos, 255, 200, 0);
                canvas_add_pixel(31 - pos, 255, 200, 0);
            } else if (step < 16) {
                if (step & 1) canvas_add_side(2, 255, 30, 0);
            }
            break;
        }
        case CARTOON_ANIM_DIZZY_STARS: {
            // 4 ngôi sao Vàng-Xanh cách đều 8 LED bay vòng tròn quanh đầu (khung vuông)
            if (step < 20) {
                for (int s = 0; s < 4; s++) {
                    int star_pos = WRAP32(s * 8 + step * 2);
                    canvas_add_pixel(star_pos, 255, 230, 0);
                    canvas_add_pixel(star_pos - 1, 50, 255, 100);
                }
            }
            break;
        }
        case CARTOON_ANIM_MAGIC_WAND: {
            // Đũa thần quét vòng quanh 4 cạnh rải bụi tiên tím-hồng-trắng lấp lánh
            if (step < 18) {
                int wand = WRAP32(step * 2);
                canvas_add_pixel(wand, 255, 255, 255);
                canvas_add_pixel(wand - 1, 220, 50, 255);
                for (int k = 0; k < 3; k++) {
                    canvas_add_pixel(rand() % 32, 200, 100, 255);
                }
            }
            break;
        }
        case CARTOON_ANIM_TOM_JERRY_CHASE: {
            // Mèo Tom (Cụm Xanh Dương 3 LED) rượt đuổi Chuột Jerry (Cụm Vàng Cam 2 LED) quanh 4 cạnh!
            if (step < 22) {
                int jerry = WRAP32(step * 3 + 7);
                int tom   = WRAP32(step * 3);
                // Vẽ Chuột Jerry chạy trước
                canvas_set_pixel(jerry,     255, 210, 0);
                canvas_set_pixel(jerry - 1, 200, 120, 0);
                // Vẽ Mèo Tom đuổi sát phía sau
                canvas_set_pixel(tom,       0, 140, 255);
                canvas_set_pixel(tom - 1,   0, 80,  220);
                canvas_set_pixel(tom - 2,   0, 40,  150);
            }
            break;
        }
        case CARTOON_ANIM_PUNCH_IMPACT: {
            // Cú đấm K.O.: 2 cạnh đối diện đập chấn động Đỏ-Trắng
            if (step < 10) {
                uint8_t s1 = (step & 2) ? 0 : 1;
                uint8_t s2 = s1 + 2;
                canvas_add_side(s1, r, g, b);
                canvas_add_side(s2, r, g, b);
                if (step < 3) canvas_add_corners(255, 255, 255);
            }
            break;
        }
    }
}

// ============================================================================
// VÒNG LẶP ĐỒ HỌA 40 FPS (GỌI MỖI 25MS BỞI BACKGROUND TASK)
// ============================================================================

void led_effects_tick(uint32_t delta_ms) {
    if (!s_led_strip || !s_led_mutex) return;
    if (s_current_style == LED_STYLE_OFF) return;

    // 1. Xử lý các chế độ đèn nền chủ động (Ambient Styles từ BTN 4)
    if (s_current_style == LED_STYLE_RAINBOW) {
        if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(3)) == pdTRUE) {
            s_style_step = (s_style_step + 4) % 360;
            for (int i = 0; i < LED_STRIP_COUNT; i++) {
                uint8_t r, g, b;
                hsv_to_rgb((s_style_step + (i * 360) / LED_STRIP_COUNT) % 360, 100, 80, &r, &g, &b);
                led_strip_set_pixel(s_led_strip, i, r, g, b);
            }
            led_strip_refresh(s_led_strip);
            xSemaphoreGive(s_led_mutex);
        }
        return;
    } else if (s_current_style == LED_STYLE_BREATHING) {
        led_effects_idle_step();
        return;
    } else if (s_current_style == LED_STYLE_STARRY) {
        // Không return: để engine bên dưới tiếp tục render sóng lan & mờ dần của các tia sao
        s_style_step++;
        if (s_style_step % 4 == 0) {
            led_effects_spark();
        }
    }

    // 2. Xử lý đếm số đồng bộ theo thời gian thực (Mode 9 Counting)
    if (s_anim_type == LED_ANIM_COUNTING) {
        s_count_wait_ms -= (int32_t)delta_ms;
        if (s_count_wait_ms <= 0) {
            uint8_t cur = s_count_current + 1;
            s_count_current = cur;
            uint8_t r, g, b;
            hsv_to_rgb((cur * 32) % 360, 100, 100, &r, &g, &b);
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

    // 3. KẾT XUẤT KHUNG HÌNH ĐỘNG 40 FPS TRÊN KHUNG HÌNH VUÔNG 4x8
    bool has_active_ripples = false;
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (s_ripples[i].active) {
            has_active_ripples = true;
            break;
        }
    }

    bool need_render = s_canvas_active || has_active_ripples ||
                       (s_anim_type != LED_ANIM_NONE) ||
                       s_dj_beat_active || s_mozart_autoplay ||
                       (s_mozart_bass_glow > 0) || (s_race_active_ms > 0) ||
                       s_tw_page_active;

    if (need_render) {
        if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(4)) == pdTRUE) {
            // Bước A: Làm mờ tự nhiên khung hình trước (Smooth Trail Afterglow Decay)
            uint16_t max_brightness = 0;
            for (int i = 0; i < LED_STRIP_COUNT; i++) {
                s_canvas_r[i] = (uint8_t)(((uint16_t)s_canvas_r[i] * s_decay_rate) >> 8);
                s_canvas_g[i] = (uint8_t)(((uint16_t)s_canvas_g[i] * s_decay_rate) >> 8);
                s_canvas_b[i] = (uint8_t)(((uint16_t)s_canvas_b[i] * s_decay_rate) >> 8);
                if (s_canvas_r[i] < 2) s_canvas_r[i] = 0;
                if (s_canvas_g[i] < 2) s_canvas_g[i] = 0;
                if (s_canvas_b[i] < 2) s_canvas_b[i] = 0;
            }

            // Bước B: Lớp nền theo Chế độ đang hoạt động (Mozart / DJ / Speed / Typewriter)
            if (s_mozart_autoplay) {
                s_mozart_aurora_step = (s_mozart_aurora_step + 3) % 360;
                for (int i = 0; i < LED_STRIP_COUNT; i++) {
                    uint8_t ar, ag, ab;
                    hsv_to_rgb((s_mozart_aurora_step + i * 11) % 360, 85, 22, &ar, &ag, &ab);
                    canvas_add_pixel(i, ar, ag, ab);
                }
            } else if (s_mozart_progress_leds > 0) {
                for (int i = 0; i < s_mozart_progress_leds && i < LED_STRIP_COUNT; i++) {
                    canvas_add_pixel(i, 6, 12, 32);
                }
            }

            if (s_mozart_bass_glow > 0) {
                // 4 góc vuông ấm áp theo nhịp Bass hòa thanh
                uint8_t bg = s_mozart_bass_glow;
                canvas_add_corners(bg, bg / 2, 0);
                s_mozart_bass_glow = (bg > 35) ? (bg - 35) : 0;
            }

            if (s_dj_beat_active) {
                // Nhịp nền DJ 120 BPM: Thắp sáng nền cạnh hiện tại (xoay vòng 4 cạnh theo phách)
                uint8_t active_side = (s_dj_beat_step >> 1) & 0x03;
                canvas_add_side(active_side, 18, 8, 38);
                // Chấm sáng quét dọc cạnh theo từng nốt móc đơn
                int scan_pos = active_side * 8 + ((s_dj_beat_step & 1) ? 5 : 2);
                canvas_add_pixel(scan_pos, 0, 90, 120);
            }

            if (s_tw_page_active) {
                // Giữ sáng các ký tự đã gõ trên 4 cạnh trang giấy máy chữ
                for (int i = 0; i < s_tw_col && i < LED_STRIP_COUNT; i++) {
                    bool is_corner = ((i & 7) == 0);
                    canvas_set_pixel(i, is_corner ? 65 : 38, is_corner ? 42 : 24, 6);
                }
                // Con trỏ đầu đọc nhấp nháy hoặc cảnh báo đỏ cam khi gần hết trang (cột >= 26)
                if (s_tw_col < LED_STRIP_COUNT) {
                    if (s_tw_head_flash > 0) {
                        canvas_set_pixel(s_tw_col, 255, 250, 200);
                        s_tw_head_flash--;
                    } else if (s_tw_col >= 26) {
                        uint8_t warn = ((s_idle_step++ & 2) ? 255 : 120);
                        canvas_set_pixel(s_tw_col, warn, 35, 0);
                    } else {
                        canvas_set_pixel(s_tw_col, 210, 185, 110);
                    }
                }
            }

            if (s_race_active_ms > 0) {
                // Đường đua F1 hình vuông khép kín (Mode 3 Speed Runner)
                if (s_race_active_ms > delta_ms) s_race_active_ms -= delta_ms;
                else s_race_active_ms = 0;

                if (s_race_speed_level == 0) {
                    // Xe dừng: 4 góc nhấp nháy đèn hậu đỏ nhẹ
                    canvas_add_corners(45, 0, 0);
                } else {
                    s_race_car_pos += s_race_car_speed;
                    if (s_race_car_pos >= 32.0f) s_race_car_pos -= 32.0f;
                    int head = WRAP32((int)s_race_car_pos);

                    if (s_race_speed_level >= 4) {
                        // Cấp 4 NITRO: Vòng xoáy cầu vồng siêu tốc + Lửa xanh ở 4 góc
                        for (int i = 0; i < LED_STRIP_COUNT; i++) {
                            uint8_t r, g, b;
                            hsv_to_rgb(((int)(s_race_car_pos * 12) + i * 11) % 360, 100, 90, &r, &g, &b);
                            canvas_set_pixel(i, r, g, b);
                        }
                        canvas_add_corners(150, 255, 255);
                    } else {
                        // Vẽ đuôi lửa xe đua chạy vòng quanh 4 cạnh
                        int tail_len = 3 + s_race_speed_level * 3;
                        for (int t = tail_len; t >= 1; t--) {
                            uint8_t bright = (uint8_t)((220 * (tail_len - t + 1)) / (tail_len + 1));
                            if (s_race_speed_level == 1) {
                                canvas_add_pixel(head - t, 0, bright, bright / 3);
                            } else if (s_race_speed_level == 2) {
                                canvas_add_pixel(head - t, bright, (bright * 2) / 3, 0);
                            } else {
                                canvas_add_pixel(head - t, bright, bright / 5, 0);
                            }
                        }
                        // Đầu xe đèn pha Trắng rực rỡ
                        canvas_set_pixel(head, 255, 255, 255);
                        canvas_add_pixel(head + 1, 180, 220, 255);
                    }
                }
            }

            // Bước C: Cập nhật & Vẽ các Sóng Lan Tỏa Đa Âm (Polyphonic Square Ripples)
            for (int i = 0; i < MAX_RIPPLES; i++) {
                if (!s_ripples[i].active) continue;
                s_ripples[i].step++;
                uint8_t st = s_ripples[i].step;
                uint8_t max_st = s_ripples[i].max_steps;
                if (st > max_st) {
                    s_ripples[i].active = false;
                    continue;
                }
                uint8_t scale = (uint8_t)((255 * (max_st - st + 1)) / (max_st + 1));
                uint8_t rr = (uint8_t)(((uint16_t)s_ripples[i].r * scale) >> 8);
                uint8_t gg = (uint8_t)(((uint16_t)s_ripples[i].g * scale) >> 8);
                uint8_t bb = (uint8_t)(((uint16_t)s_ripples[i].b * scale) >> 8);

                int c = s_ripples[i].center;
                if (s_ripples[i].style == 1) {
                    // 8-Bit Mario: Nhảy bậc thang pixel & chớp góc cạnh
                    int step_dist = (st & ~1);
                    canvas_add_pixel(c + step_dist, rr, gg, bb);
                    canvas_add_pixel(c - step_dist, rr, gg, bb);
                } else if (s_ripples[i].style == 2) {
                    // Music Box: Rải kim tuyến lấp lánh quanh sóng và ở 4 góc
                    canvas_add_pixel(c + st, rr, gg, bb);
                    canvas_add_pixel(c - st, rr, gg, bb);
                    if (st & 1) {
                        canvas_add_pixel(s_corners[st & 3], rr, gg, bb);
                    }
                } else {
                    // Sóng mượt lan vòng qua các góc hình vuông
                    canvas_add_pixel(c + st, rr, gg, bb);
                    canvas_add_pixel(c - st, rr, gg, bb);
                    if (s_ripples[i].style == 3) {
                        // Sóng phản xạ ở cạnh đối diện
                        int opp = WRAP32(c + 16);
                        canvas_add_pixel(opp + (st / 2), rr / 2, gg / 2, bb / 2);
                        canvas_add_pixel(opp - (st / 2), rr / 2, gg / 2, bb / 2);
                    }
                }
            }

            // Bước D: Thực thi Hoạt Cảnh Chuyên Biệt (Choreographed Square Animations)
            led_anim_type_t anim = s_anim_type;
            uint16_t step = s_anim_step;

            if (anim == LED_ANIM_MODE_SWITCH) {
                // 4 tia sáng cầu vồng xuất phát từ 4 góc xoay trọn 1 vòng khung vuông
                if (step < 14) {
                    uint16_t base_hue = s_anim_hue;
                    for (int s = 0; s < 4; s++) {
                        int pos = WRAP32(s * 8 + step);
                        uint8_t r, g, b;
                        hsv_to_rgb((base_hue + s * 60 + step * 12) % 360, 100, 100, &r, &g, &b);
                        canvas_set_pixel(pos, r, g, b);
                        canvas_add_pixel(pos - 1, r / 2, g / 2, b / 2);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_CELEBRATION) {
                // Pháo hoa 4 cạnh & vòng xoáy cầu vồng rực rỡ
                if (step < 24) {
                    for (int i = 0; i < LED_STRIP_COUNT; i++) {
                        uint8_t r, g, b;
                        hsv_to_rgb((step * 22 + i * 11) % 360, 100, 90, &r, &g, &b);
                        canvas_set_pixel(i, r, g, b);
                    }
                    if ((step & 1) == 0) {
                        canvas_add_corners(255, 255, 255);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_SQUARE_SPIN) {
                // Sao băng xoay 1 vòng 360° quanh 4 cạnh (Thuận / Ngược chiều kim đồng hồ)
                if (step < 16) {
                    int dir = (s_anim_sub != 0) ? 1 : -1;
                    int pos1 = WRAP32(dir * (int)step * 2);
                    int pos2 = WRAP32(pos1 + 16);
                    canvas_set_pixel(pos1, s_anim_r, s_anim_g, s_anim_b);
                    canvas_add_pixel(pos1 - dir, s_anim_r / 2, s_anim_g / 2, s_anim_b / 2);
                    canvas_set_pixel(pos2, s_anim_r, s_anim_g, s_anim_b);
                    canvas_add_pixel(pos2 - dir, s_anim_r / 2, s_anim_g / 2, s_anim_b / 2);
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_DIRECTION_SWEEP) {
                // Từ LED trung tâm cạnh `side` (side*8+3 và side*8+4), 2 nhánh chạy ngược chiều nhau
                // quanh khung vuông, sáng dần (giữ lại vệt) và gặp nhau ở tâm cạnh đối diện (sau 15 bước)
                if (step < 24) {
                    int side = s_anim_sub & 0x03;
                    int c1 = side * 8 + 3;
                    int c2 = side * 8 + 4;
                    int reach = (step > 15) ? 15 : (int)step;
                    for (int d = 0; d <= reach; d++) {
                        // Đầu sóng trắng sáng nhất, vệt phía sau mang màu chủ đạo mờ dần
                        uint8_t fade = (uint8_t)(255 - ((reach - d) * 11));
                        uint8_t rr = (uint8_t)(((uint16_t)s_anim_r * fade) >> 8);
                        uint8_t gg = (uint8_t)(((uint16_t)s_anim_g * fade) >> 8);
                        uint8_t bb = (uint8_t)(((uint16_t)s_anim_b * fade) >> 8);
                        if (d == reach && step <= 15) { rr = 255; gg = 255; bb = 255; }
                        canvas_set_pixel(c1 - d, rr, gg, bb);
                        canvas_set_pixel(c2 + d, rr, gg, bb);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_TURN_SIGNAL) {
                // Xi-nhan chạy đuổi tuần tự kiểu Audi trên Cạnh Trái (Side 3) hoặc Cạnh Phải (Side 1)
                if (step < 16) {
                    // Cạnh Trái = LED 24..31 (Side 3), Cạnh Phải = LED 8..15 (Side 1)
                    bool is_left = (s_anim_sub != 0);
                    int base = is_left ? 24 : 8;
                    int prog = (step % 8) + 1;
                    for (int k = 0; k < prog; k++) {
                        canvas_set_pixel(base + k, 255, 130, 0);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_WATER_CANNON) {
                // Vòi rồng phun 2 luồng nước Xanh-Trắng từ Cạnh Dưới dọc 2 bên dập lửa ở Cạnh Trên
                if (step < 20) {
                    for (int k = 0; k < 4; k++) {
                        int d = (step + k * 4) % 17;
                        uint8_t w = (k == 0) ? 200 : 40;
                        canvas_set_pixel(19 - d, w, 220, 255);
                        canvas_set_pixel(20 + d, w, 220, 255);
                    }
                    // Bọt nước trắng xóa tung tóe ở Cạnh Trên (0..7)
                    if (step > 4) {
                        for (int i = 0; i < 8; i++) {
                            canvas_add_pixel(i, (rand() % 120), 200, 255);
                        }
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_FIRE_FLICKER) {
                // Đám cháy bập bùng động trên cả 4 cạnh hình vuông
                if (step < 22) {
                    for (int i = 0; i < LED_STRIP_COUNT; i++) {
                        uint8_t heat = 140 + (rand() % 115);
                        uint8_t green = (heat > 200) ? (rand() % 95) : (rand() % 35);
                        canvas_set_pixel(i, heat, green, 0);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_SEARCHLIGHT) {
                // Chùm đèn pha Trắng 5 LED quét vòng quanh 4 cạnh xuyên màn đêm
                if (step < 24) {
                    int beam = WRAP32(step * 2);
                    for (int w = -2; w <= 2; w++) {
                        uint8_t br = (w == 0) ? 255 : (abs(w) == 1 ? 180 : 80);
                        canvas_set_pixel(beam + w, br, br, br);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_HELICOPTER) {
                // Cánh quạt trực thăng 4 cánh (cách đều 8 LED) quay tít 360° quanh khung vuông
                if (step < 24) {
                    for (int blade = 0; blade < 4; blade++) {
                        int pos = WRAP32(blade * 8 + step * 2);
                        canvas_set_pixel(pos,     220, 255, 255);
                        canvas_set_pixel(pos - 1, 0,   160, 255);
                        canvas_set_pixel(pos - 2, 0,   60,  140);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_RADAR_SONAR) {
                // Tia Radar xanh lục bảo quét 360° quanh khung vuông + Nháy đỏ mục tiêu
                if (step < 24) {
                    int sweep = WRAP32(step * 2);
                    for (int t = 0; t < 7; t++) {
                        uint8_t g_val = 255 - t * 35;
                        canvas_add_pixel(sweep - t, 0, g_val, g_val / 4);
                    }
                    // Khi tia radar quét qua tọa độ mục tiêu (LED 12 và LED 26) -> Nháy Đỏ Báo Động!
                    if (abs(sweep - 12) <= 2 || abs(sweep - 26) <= 2) {
                        canvas_set_pixel(12, 255, 0, 0);
                        canvas_set_pixel(13, 255, 50, 0);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_MORSE_STREAM) {
                // Hiển thị chuỗi Chấm (.) và Gạch (-) mã Morse chạy truyền tin quanh khung vuông
                if (step < 16) {
                    int cursor = WRAP32(step);
                    for (int m = 0; s_morse_buf[m] != '\0' && m < 8; m++) {
                        if (s_morse_buf[m] == '.') {
                            // Dấu Chấm (Dot): 2 LED Xanh Lá Matrix
                            canvas_set_pixel(cursor,     0, 255, 60);
                            canvas_set_pixel(cursor + 1, 0, 200, 40);
                            cursor = WRAP32(cursor + 4);
                        } else if (s_morse_buf[m] == '-') {
                            // Dấu Gạch (Dash): 5 LED Xanh Cyan Sáng Rực
                            for (int k = 0; k < 5; k++) {
                                canvas_set_pixel(cursor + k, 0, 220, 255);
                            }
                            cursor = WRAP32(cursor + 7);
                        }
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_R2D2_HOLO) {
                // 8 cụm dữ liệu R2-D2 (mỗi cạnh 2 cụm 4 LED) nhấp nháy Xanh-Trắng-Đỏ
                if (step < 14) {
                    for (int cell = 0; cell < 8; cell++) {
                        int pick = (cell + step * 3 + (rand() & 3)) & 3;
                        uint8_t r = 0, g = 0, b = 0;
                        if (pick == 0)      { r = 0;   g = 120; b = 255; } // R2 Blue
                        else if (pick == 1) { r = 220; g = 240; b = 255; } // Droid White
                        else if (pick == 2) { r = 255; g = 20;  b = 20;  } // Logic Red
                        for (int k = 0; k < 3; k++) {
                            canvas_set_pixel(cell * 4 + k, r, g, b);
                        }
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_TYPEWRITER_RETURN) {
                // Đầu đọc máy chữ trượt vèo ngược từ vị trí hiện tại về 0
                if (s_anim_sub > 0) {
                    int cur = s_anim_sub;
                    int next = (cur > 4) ? (cur - 4) : 0;
                    for (int p = next; p <= cur; p++) {
                        canvas_set_pixel(p, 255, 220, 60);
                    }
                    s_anim_sub = (uint8_t)next;
                } else {
                    canvas_set_pixel(0, 255, 255, 255);
                    s_anim_type = LED_ANIM_NONE;
                    s_tw_page_active = true;
                    s_tw_col = 0;
                }
            } else if (anim == LED_ANIM_CARTOON_FX) {
                if (step < 20) {
                    render_cartoon_anim_step((cartoon_led_anim_t)s_anim_sub, step, s_anim_r, s_anim_g, s_anim_b);
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_DRUM_CYMBAL_SHIMMER) {
                // Chũm chọe Cymbal rung kim loại lấp lánh khắp 4 cạnh rồi dịu dần
                if (step < 16) {
                    uint8_t fade = (uint8_t)(240 - step * 14);
                    for (int i = 0; i < LED_STRIP_COUNT; i++) {
                        if ((i + step) % 3 == 0) {
                            canvas_add_pixel(i, fade, fade, fade / 2);
                        } else if ((rand() & 3) == 0) {
                            canvas_add_pixel(i, 0, fade, fade);
                        }
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_RADIO_WAVE) {
                // Sóng âm bộ đàm 114 nhấp nhô từ 4 tâm cạnh
                if (step < 16) {
                    int span = (step & 3) + 1;
                    for (int s = 0; s < 4; s++) {
                        int c = s_side_centers[s];
                        for (int w = 0; w < span; w++) {
                            canvas_set_pixel(c - w, 0, 200, 255);
                            canvas_set_pixel(c + 1 + w, 0, 200, 255);
                        }
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_BELL_RING) {
                // Chuông báo cháy đập quả lắc vàng-đỏ giữa các cạnh đối diện
                if (step < 16) {
                    uint8_t side_a = (step & 2) ? 0 : 1;
                    canvas_add_side(side_a, 255, 220, 0);
                    canvas_add_side(side_a + 2, 255, 60, 0);
                    canvas_add_corners(255, 255, 200);
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_REVERSE_BEEP) {
                // Số lùi xe cứu hỏa: Cạnh dưới + 4 góc chớp Vàng-Đỏ-Trắng theo nhịp Bíp... Bíp...
                if (step < 18) {
                    if ((step % 6) < 3) {
                        canvas_add_side(2, 255, 190, 0);
                        canvas_add_corners(255, 40, 0);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            } else if (anim == LED_ANIM_ACCEL_BOOST) {
                // Đạp ga tăng tốc: Luồng lửa Đỏ-Cam-Vàng phóng dọc 2 cạnh bên lên phía trước
                if (step < 14) {
                    int d = step * 2;
                    for (int k = 0; k < 4; k++) {
                        int pos_l = WRAP32(19 - d + k);
                        int pos_r = WRAP32(20 + d - k);
                        canvas_add_pixel(pos_l, 255, 160 - k * 35, 0);
                        canvas_add_pixel(pos_r, 255, 160 - k * 35, 0);
                    }
                    s_anim_step = step + 1;
                } else {
                    s_anim_type = LED_ANIM_NONE;
                }
            }

            // Bước E: Kiểm tra xem Canvas còn điểm sáng nào không và xuất ra dải LED
            for (int i = 0; i < LED_STRIP_COUNT; i++) {
                uint16_t sum = (uint16_t)s_canvas_r[i] + s_canvas_g[i] + s_canvas_b[i];
                if (sum > max_brightness) max_brightness = sum;
            }

            flush_canvas_locked();
            xSemaphoreGive(s_led_mutex);

            if (max_brightness == 0 && !has_active_ripples && s_anim_type == LED_ANIM_NONE &&
                !s_dj_beat_active && !s_mozart_autoplay && s_race_active_ms == 0 && !s_tw_page_active) {
                s_canvas_active = false;
            }
        }
        return;
    }

    // 4. Khi không gõ phím sau 4.5 giây -> Đèn thở đối xứng 4 cạnh êm dịu
    s_idle_elapsed_ms += delta_ms;
    if (s_idle_elapsed_ms >= 4500) {
        led_effects_idle_step();
    }
}

// ============================================================================
// CÁC HÀM HIỆU ỨNG DÙNG CHUNG & TƯƠNG THÍCH HỆ THỐNG
// ============================================================================

void led_effects_mode_switch(uint8_t mode_index) {
    s_anim_hue = (mode_index * 40) % 360;
    s_anim_step = 0;
    s_decay_rate = 185;
    s_dj_beat_active = false;
    s_mozart_autoplay = false;
    s_mozart_progress_leds = 0;
    s_race_active_ms = 0;
    s_tw_page_active = false;
    s_anim_type = LED_ANIM_MODE_SWITCH;
    s_idle_elapsed_ms = 0;
    s_canvas_active = true;
}

void led_effects_celebration(void) {
    s_anim_step = 0;
    s_decay_rate = 200;
    s_anim_type = LED_ANIM_CELEBRATION;
    s_idle_elapsed_ms = 0;
    s_canvas_active = true;
}

void led_effects_start_counting(uint8_t target_count, uint32_t delay_ms, uint32_t step_ms) {
    if (target_count == 0) {
        // Số 0: Vòng sáng thu về 4 tâm cạnh rồi tắt êm
        s_anim_type = LED_ANIM_NONE;
        led_effects_piano_chord(0, 200, 255);
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

// Nốt nhạc với sóng lan tỏa vòng quanh khung hình vuông
void led_effects_note(uint8_t note_idx, uint8_t total_notes, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 205;
    int center = (note_idx * (LED_STRIP_COUNT - 1)) / (total_notes > 0 ? total_notes : 1);
    spawn_square_ripple((uint8_t)center, 10, r, g, b, 0);
    canvas_set_pixel(center, 255, 255, 255); // Tâm nốt lóe trắng sáng rực
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

// Sóng xung kích trống Bass từ 4 tâm cạnh dạt ra 4 góc vuông
void led_effects_beat(void) {
    led_effects_drum_kick(0);
}

// Tia lửa kim tuyến lấp lánh tại vị trí ngẫu nhiên & góc vuông
void led_effects_spark(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 195;
    int pos = rand() % LED_STRIP_COUNT;
    uint8_t r, g, b;
    hsv_to_rgb(rand() % 360, 80, 100, &r, &g, &b);
    spawn_square_ripple((uint8_t)pos, 6, r, g, b, 2);
    canvas_set_pixel(pos, 255, 255, 220);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

// Nâng cấp color_burst: Thay vì tô tĩnh đứng im, nổ sóng đối xứng từ 4 tâm cạnh tỏa ra 4 góc và tắt dần mượt mà!
void led_effects_color_burst(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 205;
    for (int s = 0; s < 4; s++) {
        spawn_square_ripple(s_side_centers[s], 6, r, g, b, 0);
    }
    canvas_add_corners(255, 255, 255);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_matrix(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 210;
    int pos = rand() % LED_STRIP_COUNT;
    spawn_square_ripple((uint8_t)pos, 8, 0, 255, 50, 1);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

// Đèn còi ưu tiên Cứu Hỏa 114 trên Khung Hình Vuông:
// 2 cạnh đối diện Đỏ + 2 cạnh đối diện Xanh Dương đảo nhau + 4 góc chớp Strobe Trắng!
void led_effects_police(bool toggle) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(false);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 248; // Giữ sáng đều giữa 2 lần đảo màu, không bị mờ nhấp nhả
    canvas_clear_mem();
    if (toggle) {
        canvas_add_side(0, 255, 0, 0);   // Cạnh Trên: Đỏ
        canvas_add_side(2, 255, 0, 0);   // Cạnh Dưới: Đỏ
        canvas_add_side(1, 0, 40, 255);  // Cạnh Phải: Xanh
        canvas_add_side(3, 0, 40, 255);  // Cạnh Trái: Xanh
    } else {
        canvas_add_side(0, 0, 40, 255);  // Cạnh Trên: Xanh
        canvas_add_side(2, 0, 40, 255);  // Cạnh Dưới: Xanh
        canvas_add_side(1, 255, 0, 0);   // Cạnh Phải: Đỏ
        canvas_add_side(3, 255, 0, 0);   // Cạnh Trái: Đỏ
    }
    // 4 góc vuông chớp trắng cứu hộ
    canvas_add_corners(220, 220, 255);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_water_spray(void) {
    touch_led_activity(true);
    s_decay_rate = 190;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_WATER_CANNON;
}

void led_effects_turn_signal(bool left) {
    touch_led_activity(true);
    s_decay_rate = 180;
    s_anim_sub = left ? 1 : 0;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_TURN_SIGNAL;
}

void led_effects_searchlight(void) {
    touch_led_activity(true);
    s_decay_rate = 185;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_SEARCHLIGHT;
}

void led_effects_fire_glow(void) {
    touch_led_activity(true);
    s_decay_rate = 210;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_FIRE_FLICKER;
}

void led_effects_rescue_success(void) {
    led_effects_celebration();
}

// Đèn thở đối xứng 4 cạnh hình vuông khi chờ (Idle Mode)
void led_effects_idle_step(void) {
    if (!s_led_strip || !s_led_mutex) return;
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_idle_step++;
    float rad = (float)s_idle_step * 0.07f;
    float brightness = (sinf(rad) + 1.0f) * 0.5f * 28.0f + 4.0f;

    for (int side = 0; side < 4; side++) {
        for (int k = 0; k < 8; k++) {
            int dist_center = abs(k - 3);
            uint8_t local_v = (uint8_t)(brightness * (8 - dist_center) / 8.0f);
            uint8_t r, g, b;
            hsv_to_rgb((s_idle_step * 2 + side * 45 + k * 8) % 360, 85, local_v, &r, &g, &b);
            led_strip_set_pixel(s_led_strip, side * 8 + k, r, g, b);
        }
    }
    led_strip_refresh(s_led_strip);
    xSemaphoreGive(s_led_mutex);
}

// Đếm số trên khung hình vuông: Mỗi bóng một màu cầu vồng tươi sáng + pháo hoa khi đếm đủ
void led_effects_count(uint8_t count, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    s_idle_elapsed_ms = 0;
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    canvas_clear_mem();
    if (count > LED_STRIP_COUNT) count = LED_STRIP_COUNT;

    for (int i = 0; i < count; i++) {
        uint8_t cr, cg, cb;
        hsv_to_rgb((i * 32) % 360, 95, 100, &cr, &cg, &cb);
        canvas_set_pixel(i, cr, cg, cb);
    }
    if (count > 0) {
        // Điểm mới nhất lóe trắng-vàng dẫn đường
        canvas_set_pixel(count - 1, 255, 255, 220);
        if (count == s_count_target) {
            canvas_add_corners(r, g, b);
        }
    }
    s_decay_rate = 242; // Giữ sáng lâu để bé quan sát số lượng bóng đèn
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_carriage(uint8_t col, uint8_t max_col) {
    led_effects_typewriter_key(col, max_col, false);
}

// Hiển thị âm lượng (Volume) đối xứng trên cả 4 cạnh hình vuông
void led_effects_volume(float volume) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    canvas_clear_mem();
    int active_leds = (int)(volume * LED_STRIP_COUNT + 0.5f);
    if (active_leds > LED_STRIP_COUNT) active_leds = LED_STRIP_COUNT;

    for (int i = 0; i < active_leds; i++) {
        float ratio = (float)i / (float)LED_STRIP_COUNT;
        uint8_t r = 0, g = 0, b = 0;
        if (ratio < 0.60f)      { r = 0;   g = 200; b = 80; }
        else if (ratio < 0.85f) { r = 240; g = 180; b = 0;  }
        else                    { r = 255; g = 30;  b = 30; }
        canvas_set_pixel(i, r, g, b);
    }
    s_decay_rate = 235;
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_mozart_note(float freq, float bass_freq, uint8_t timbre) {
    led_effects_mozart_progress(freq, bass_freq, timbre, 0, 0, s_mozart_autoplay);
}

void led_effects_speedometer(uint8_t speed_level, float speed_ratio) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 195;
    s_race_active_ms = 600;
    if (speed_level == 0) {
        s_race_speed_level = 0;
        s_race_car_speed = 0.0f;
    } else if (speed_ratio >= 0.72f || speed_level >= 24) {
        s_race_speed_level = 4; // Nitro Turbo
        s_race_car_speed = 2.2f;
    } else if (speed_ratio >= 0.45f) {
        s_race_speed_level = 3;
        s_race_car_speed = 1.4f;
    } else if (speed_ratio >= 0.22f) {
        s_race_speed_level = 2;
        s_race_car_speed = 0.9f;
    } else {
        s_race_speed_level = 1;
        s_race_car_speed = 0.5f;
    }

    // Mỗi cú gõ phím tạo thêm 1 tia lửa bứt tốc tại vị trí xe đua
    s_race_car_pos += 1.0f;
    if (s_race_car_pos >= 32.0f) s_race_car_pos -= 32.0f;
    int head = WRAP32((int)s_race_car_pos);
    canvas_set_pixel(head, 255, 255, 255);
    canvas_add_pixel(head - 1, 255, 140, 0);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_speed_brake(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(3)) != pdTRUE) return;
    s_decay_rate = 195;
    for (int i = 0; i < LED_STRIP_COUNT; i++) canvas_set_pixel(i, 255, 0, 0);
    canvas_add_corners(255, 180, 0);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_speed_nitro(void) {
    led_effects_square_spin(true, 0, 220, 255);
}

// ============================================================================
// CÁC HÀM CHUYÊN BIỆT CHO TỪNG CHẾ ĐỘ TRÊN KHUNG HÌNH VUÔNG 4x8
// ============================================================================

// Thắp sáng 1 cạnh (0=Trên, 1=Phải, 2=Dưới, 3=Trái) và phóng sóng sang 2 cạnh bên
// Thắp sáng 1 cạnh (0=Trên, 1=Phải, 2=Dưới, 3=Trái) và phóng sóng sang 2 cạnh bên
void led_effects_direction_sweep(uint8_t side, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    s_decay_rate = 215;
    s_anim_sub = side & 0x03;
    s_anim_r = r;
    s_anim_g = g;
    s_anim_b = b;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_DIRECTION_SWEEP;
}

void led_effects_square_side_pulse(uint8_t side_idx, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 200;
    uint8_t s = side_idx & 0x03;
    canvas_add_side(s, r, g, b);
    spawn_square_ripple(s_side_centers[s], 9, r, g, b, 3);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_square_spin(bool clockwise, uint8_t r, uint8_t g, uint8_t b) {
    touch_led_activity(true);
    s_decay_rate = 190;
    s_anim_sub = clockwise ? 1 : 0;
    s_anim_r = r;
    s_anim_g = g;
    s_anim_b = b;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_SQUARE_SPIN;
}

// Mode 1: Đàn Piano trên 4 cạnh hình vuông (4 hàng phím = 4 cạnh, hỗ trợ đa âm & 3 âm sắc)
void led_effects_piano_key(uint8_t row_side, uint8_t pos_in_side, uint8_t timbre, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 212; // Độ ngân sáng dài theo tiếng đàn Piano
    uint8_t center = WRAP32((row_side & 0x03) * 8 + (pos_in_side & 0x07));

    if (timbre == 1) {
        // 8-Bit Mario: Nháy cả cạnh 8 LED nhẹ + khối sóng pixel
        canvas_add_side(row_side & 0x03, r / 3, g / 3, b / 3);
        spawn_square_ripple(center, 8, r, g, b, 1);
    } else if (timbre == 2) {
        // Music Box: Sóng kim tuyến lấp lánh + 4 góc vuông sáng nhẹ
        spawn_square_ripple(center, 11, r, g, b, 2);
        canvas_add_corners(r / 3, g / 3, b / 3);
    } else {
        // Grand Piano: Sóng lan mượt mà + phản xạ hòa âm ở cạnh đối diện
        spawn_square_ripple(center, 12, r, g, b, 3);
    }
    canvas_set_pixel(center, 255, 255, 255);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_piano_chord(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 218;
    for (int s = 0; s < 4; s++) {
        spawn_square_ripple(s_side_centers[s], 8, r, g, b, 0);
        canvas_set_pixel(s_side_centers[s], 255, 255, 255);
    }
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_pentatonic_toggle(bool enabled) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 205;
    for (int i = 0; i < 4; i++) {
        if (enabled) {
            // Bật Ngũ Cung: Phóng sóng Vàng Kim từ 4 Góc Vuông chụm vào 4 Cạnh
            spawn_square_ripple(s_corners[i], 7, 255, 215, 0, 0);
        } else {
            // Tắt Ngũ Cung: Phóng sóng Xanh Ngọc từ 4 Tâm Cạnh ra 4 Góc
            spawn_square_ripple(s_side_centers[i], 7, 0, 220, 255, 0);
        }
    }
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

// Mode 2: Baby Mozart (Sao chổi giao hưởng + 4 góc Bass + Vòng tiến độ 32 LED)
void led_effects_mozart_progress(float freq, float bass_freq, uint8_t timbre, uint16_t note_idx, uint16_t total_notes, bool autoplay) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(false);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 210;
    s_mozart_autoplay = autoplay;
    if (total_notes > 0) {
        s_mozart_progress_leds = (uint8_t)(((uint32_t)note_idx * LED_STRIP_COUNT) / total_notes);
    }

    float ratio = 0.5f;
    if (freq >= 130.0f) {
        ratio = log2f(freq / 130.0f) / 3.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
    }
    uint8_t center = WRAP32((int)(ratio * (LED_STRIP_COUNT - 1) + 0.5f));

    uint8_t mr, mg, mb;
    if (timbre == 1) {
        mr = (uint8_t)(120 + ratio * 135.0f);
        mg = 220;
        mb = 255;
    } else if (timbre == 2) {
        hsv_to_rgb((uint16_t)(ratio * 300.0f), 100, 95, &mr, &mg, &mb);
    } else {
        hsv_to_rgb((uint16_t)(30.0f + ratio * 220.0f), 90, 100, &mr, &mg, &mb);
    }

    spawn_square_ripple(center, 9, mr, mg, mb, (timbre == 1) ? 2 : 3);
    canvas_set_pixel(center, 255, 255, 255);

    if (bass_freq > 20.0f) {
        s_mozart_bass_glow = 220;
        canvas_add_corners(255, 140, 20);
    }

    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

// Mode 4: Dàn Trống 4 Cạnh & 4 Góc Hình Vuông
void led_effects_drum_kick(int8_t semi) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 185; // Nổ mạnh và dứt khoát
    uint8_t g_tint = (uint8_t)((semi + 3) * 18);
    for (int s = 0; s < 4; s++) {
        spawn_square_ripple(s_side_centers[s], 5, 255, g_tint, 10, 0);
        canvas_set_pixel(s_side_centers[s], 255, 180, 80);
    }
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_drum_snare(int8_t semi) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    static bool s_snare_alt = false;
    s_snare_alt = !s_snare_alt;
    s_decay_rate = 180;
    uint8_t b_val = (semi >= 0) ? 255 : 180;
    if (s_snare_alt) {
        canvas_add_side(0, 255, 255, b_val);
        canvas_add_side(2, 255, 255, b_val);
    } else {
        canvas_add_side(1, 200, 245, 255);
        canvas_add_side(3, 200, 245, 255);
    }
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_drum_hihat(int8_t semi) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    (void)semi;
    s_decay_rate = 175;
    canvas_add_corners(255, 240, 40);
    for (int c = 0; c < 4; c++) {
        spawn_square_ripple(s_corners[c], 3, 255, 200, 0, 2);
    }
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_drum_tom(uint8_t tom_idx) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 200;
    // Phân bổ 10 trống Tom (0..9) chạy vòng quanh 4 cạnh hình vuông
    uint8_t pos = WRAP32((tom_idx * LED_STRIP_COUNT) / 10);
    uint8_t r, g, b;
    hsv_to_rgb((tom_idx * 28 + 10) % 360, 100, 100, &r, &g, &b);
    spawn_square_ripple(pos, 8, r, g, b, 3); // Phản xạ sang cả cạnh đối diện!
    canvas_set_pixel(pos, 255, 255, 255);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_drum_cymbal(int8_t semi) {
    (void)semi;
    touch_led_activity(true);
    s_decay_rate = 215;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_DRUM_CYMBAL_SHIMMER;
}

void led_effects_drum_clap(void) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 195;
    for (int s = 0; s < 4; s++) {
        int p = s * 8 + (rand() % 8);
        spawn_square_ripple((uint8_t)p, 6, 255, 30, 180, 2);
        canvas_set_pixel(p, 255, 220, 255);
    }
    canvas_add_corners(255, 80, 220);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_drum_dj_beat(uint8_t beat_step, bool active) {
    s_dj_beat_active = active;
    s_dj_beat_step = beat_step;
    s_canvas_active = true;
}

// Mode 5: Máy Gõ Chữ Cổ Điển (4 dòng x 8 cột trên Khung Hình Vuông)
void led_effects_typewriter_key(uint8_t col, uint8_t max_col, bool is_space) {
    if (!s_led_strip || !s_led_mutex) return;
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_anim_type = LED_ANIM_NONE;
    s_idle_elapsed_ms = 0;
    s_decay_rate = 200;
    s_tw_page_active = true;
    s_tw_max_col = (max_col > 0) ? max_col : LED_STRIP_COUNT;
    s_tw_col = (col < LED_STRIP_COUNT) ? col : (LED_STRIP_COUNT - 1);
    s_tw_head_flash = 3;

    if (is_space) {
        spawn_square_ripple(s_tw_col, 4, 80, 180, 255, 0);
    } else {
        // Tia lửa búa gõ cơ khí lóe lên tại đầu đọc
        spawn_square_ripple(s_tw_col, 5, 255, 200, 60, 2);
    }
    canvas_set_pixel(s_tw_col, 255, 255, 220);
    s_canvas_active = true;
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_typewriter_backspace(uint8_t col, uint8_t max_col) {
    if (!s_led_strip || !s_led_mutex) return;
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_anim_type = LED_ANIM_NONE;
    s_idle_elapsed_ms = 0;
    s_tw_page_active = true;
    s_tw_max_col = (max_col > 0) ? max_col : LED_STRIP_COUNT;
    s_tw_col = (col < LED_STRIP_COUNT) ? col : (LED_STRIP_COUNT - 1);
    // Lóe đỏ cam tại ký tự vừa xóa
    spawn_square_ripple(s_tw_col + 1, 4, 255, 50, 0, 0);
    s_canvas_active = true;
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_typewriter_return(uint8_t from_col) {
    s_tw_page_active = false;
    s_decay_rate = 185;
    s_anim_sub = (from_col > 0 && from_col < LED_STRIP_COUNT) ? from_col : (LED_STRIP_COUNT - 1);
    s_anim_step = 0;
    s_anim_type = LED_ANIM_TYPEWRITER_RETURN;
    s_idle_elapsed_ms = 0;
    s_canvas_active = true;
}

// Mode 6: Hoạt Hình & Game SFX (14 hoạt cảnh Pixel 1D trên khung vuông)
void led_effects_cartoon_fx(cartoon_led_anim_t anim, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 190;
    s_anim_sub = (uint8_t)anim;
    s_anim_r = r;
    s_anim_g = g;
    s_anim_b = b;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_CARTOON_FX;
    render_cartoon_anim_step(anim, 0, r, g, b);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

// Mode 7: Điệp Viên Nhí & Robot R2-D2
void led_effects_morse_code(const char *morse_str) {
    if (!s_led_strip || !s_led_mutex || !morse_str) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    strncpy(s_morse_buf, morse_str, sizeof(s_morse_buf) - 1);
    s_morse_buf[sizeof(s_morse_buf) - 1] = '\0';
    s_decay_rate = 185;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_MORSE_STREAM;
    xSemaphoreGive(s_led_mutex);
}

void led_effects_radar_sweep(void) {
    touch_led_activity(true);
    s_decay_rate = 195;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_RADAR_SONAR;
}

void led_effects_r2d2_holo(void) {
    touch_led_activity(true);
    s_decay_rate = 180;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_R2D2_HOLO;
}

// Mode 8: Buồng Lái Xe Cứu Hỏa 114 & 26 Con Vật
void led_effects_animal_pulse(uint8_t animal_idx, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    s_decay_rate = 210;
    uint8_t center = WRAP32((animal_idx * LED_STRIP_COUNT) / 26);
    uint8_t side = (center / 8) & 0x03;
    canvas_add_side(side, r / 2, g / 2, b / 2);
    spawn_square_ripple(center, 12, r, g, b, 3);
    canvas_add_corners(r, g, b);
    canvas_set_pixel(center, 255, 255, 255);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}

void led_effects_cockpit_accel(void) {
    touch_led_activity(true);
    s_decay_rate = 195;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_ACCEL_BOOST;
}

void led_effects_cockpit_reverse(void) {
    touch_led_activity(true);
    s_decay_rate = 190;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_REVERSE_BEEP;
}

void led_effects_helicopter_rotor(void) {
    touch_led_activity(true);
    s_decay_rate = 185;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_HELICOPTER;
}

void led_effects_radio_wave(void) {
    touch_led_activity(true);
    s_decay_rate = 185;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_RADIO_WAVE;
}

void led_effects_bell_ring(void) {
    touch_led_activity(true);
    s_decay_rate = 190;
    s_anim_step = 0;
    s_anim_type = LED_ANIM_BELL_RING;
}

// Mode 9: Bé Học Đánh Vần (Bản đồ chữ cái A-Z trên 4 cạnh hình vuông)
void led_effects_phonics_letter(char upper_letter) {
    if (!s_led_strip || !s_led_mutex) return;
    touch_led_activity(true);
    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(2)) != pdTRUE) return;

    int idx = (upper_letter >= 'A' && upper_letter <= 'Z') ? (upper_letter - 'A') : 0;
    uint8_t center = WRAP32((idx * LED_STRIP_COUNT) / 26);
    uint8_t r, g, b;
    hsv_to_rgb((idx * 14) % 360, 100, 100, &r, &g, &b);

    s_decay_rate = 212;
    canvas_add_side((center / 8) & 0x03, r / 3, g / 3, b / 3);
    spawn_square_ripple(center, 11, r, g, b, 2);
    canvas_set_pixel(center, 255, 255, 255);
    flush_canvas_locked();
    xSemaphoreGive(s_led_mutex);
}
