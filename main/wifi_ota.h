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
#define CONFIG_WIFI_OTA_SSID "HG156"
#endif

#ifndef CONFIG_WIFI_OTA_PASS
#define CONFIG_WIFI_OTA_PASS "hg123456!"
#endif

// Static IP Configuration (Guarantees instant connection & predictable IP)
#define CONFIG_WIFI_STATIC_IP   "192.168.1.156"
#define CONFIG_WIFI_GATEWAY     "192.168.1.1"
#define CONFIG_WIFI_NETMASK     "255.255.255.0"

#define WIFI_OTA_HOSTNAME       "esp32-toy"
#define WIFI_OTA_UDP_PORT       45454
#define WIFI_OTA_HTTP_PORT      80

/**
 * @brief Initialize Wi-Fi in Station mode with Static IP and start OTA background services.
 * @return ESP_OK on success
 */
esp_err_t wifi_ota_init(void);

/**
 * @brief Check if Wi-Fi is currently connected and has an assigned IP address
 */
bool wifi_ota_is_connected(void);

/**
 * @brief Get current IP address as a string (e.g. "192.168.1.156")
 */
const char *wifi_ota_get_ip_str(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_OTA_H
