#ifndef ROTARY_ENCODER_H
#define ROTARY_ENCODER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Hardware pinout for HW-040 Rotary Encoder on ESP32-S3
#define PIN_ENCODER_CLK    14   // Rotary CLK
#define PIN_ENCODER_DT     15   // Rotary DT (Chuyển sang GPIO 15 vì GPIO 13 dùng cho dải LED)
#define PIN_ENCODER_SW     12   // Rotary SW (Push Button / Switch)

typedef void (*encoder_rotate_cb_t)(int delta);
typedef void (*encoder_btn_cb_t)(void);

esp_err_t rotary_encoder_init(encoder_rotate_cb_t on_rotate, encoder_btn_cb_t on_press);

#endif // ROTARY_ENCODER_H
