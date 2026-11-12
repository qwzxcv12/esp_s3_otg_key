#ifndef WIFI_OTA_H
#define WIFI_OTA_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wi-Fi Configuration (Target 2.4GHz network for ESP32-S3)
#ifndef CONFIG_WIFI_OTA_SSID
#define CONFIG_WIFI_OTA_SSID "HG156 2g4"
#endif

#ifndef CONFIG_WIFI_OTA_ALT_SSID
#define CONFIG_WIFI_OTA_ALT_SSID "HG156"
#endif

#ifndef CONFIG_WIFI_OTA_PASS
#define CONFIG_WIFI_OTA_PASS "hg123456!"
#endif

#define WIFI_OTA_HOSTNAME   "esp32-toy"
#define WIFI_OTA_UDP_PORT   45454
#define WIFI_OTA_HTTP_PORT  80

/**
 * @brief Initialize Wi-Fi in Station mode and start background OTA HTTP & UDP services.
 *        Does not block app_main.
 * @return ESP_OK on success
 */
esp_err_t wifi_ota_init(void);

/**
 * @brief Check if Wi-Fi is currently connected and has an assigned IP address
 */
bool wifi_ota_is_connected(void);

/**
 * @brief Get current IP address as a string (e.g. "192.168.1.50")
 */
const char *wifi_ota_get_ip_str(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_OTA_H
