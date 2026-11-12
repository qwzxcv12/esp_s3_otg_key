#ifndef PN532_DRIVER_H
#define PN532_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Kết nối giữa ESP32 (GPIO 5, GPIO 6) và 2 chân SDA, SCL của PN532
// Chế độ UART HSU tự động nhận diện chiều dây (Auto Pin Swap)
#define PIN_PN532_GPIO_A    5
#define PIN_PN532_GPIO_B    6
#define PN532_UART_PORT     UART_NUM_1
#define PN532_UART_BAUD     115200

typedef void (*pn532_card_cb_t)(const uint8_t *uid, uint8_t uid_len);

esp_err_t pn532_driver_init(pn532_card_cb_t card_cb);
bool pn532_is_connected(void);
void pn532_set_scanner_active(bool active);

#ifdef __cplusplus
}
#endif

#endif // PN532_DRIVER_H
