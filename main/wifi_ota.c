#include "wifi_ota.h"
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "mdns.h"

static const char *TAG = "WIFI_OTA";

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

static bool s_connected = false;
static char s_ip_str[32] = "0.0.0.0";
static httpd_handle_t s_http_server = NULL;
static TaskHandle_t s_udp_task_handle = NULL;
static int s_retry_count = 0;

/* HTML Web page for browser-based manual OTA upload */
static const char index_html[] = 
"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<title>ESP32-S3 Wireless OTA</title>"
"<style>"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#181825;color:#cdd6f4;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;padding:16px;box-sizing:border-box;}"
".card{background:#24273a;border-radius:16px;box-shadow:0 8px 32px rgba(0,0,0,0.4);padding:32px;width:100%;max-width:440px;text-align:center;border:1px solid #363a4f;}"
"h2{margin-top:0;color:#89b4fa;font-size:22px;}"
".badge{display:inline-block;padding:4px 12px;border-radius:20px;font-size:12px;font-weight:600;margin-bottom:18px;background:#a6e3a1;color:#11111b;}"
".file-box{margin:20px 0;padding:16px;background:#1e2030;border:2px dashed #45475a;border-radius:12px;cursor:pointer;}"
"input[type=file]{display:none;}"
"label{cursor:pointer;color:#89dceb;font-weight:500;display:block;}"
"#filename{font-size:13px;color:#a6adc8;margin-top:6px;word-break:break-all;}"
"button{background:#89b4fa;color:#11111b;border:none;padding:12px 24px;font-size:15px;font-weight:700;border-radius:8px;cursor:pointer;width:100%;transition:0.2s;}"
"button:hover{background:#b4befe;}"
"button:disabled{background:#585b70;cursor:not-allowed;}"
".progress-bar{margin-top:20px;background:#313244;border-radius:8px;height:12px;overflow:hidden;display:none;}"
".progress-fill{background:#a6e3a1;height:100%;width:0%;transition:width 0.15s;}"
"#status{margin-top:14px;font-size:14px;min-height:20px;font-weight:500;}"
"</style></head><body><div class=\"card\">"
"<h2>ESP32-S3 Toy Synthesizer</h2>"
"<div class=\"badge\">OTA Flasher Ready</div>"
"<div class=\"file-box\" onclick=\"document.getElementById('firmware').click()\">"
"<label>Chon file firmware (.bin)</label>"
"<div id=\"filename\">Chua chon file</div>"
"<input type=\"file\" id=\"firmware\" accept=\".bin\" onchange=\"fileChosen(this)\">"
"</div>"
"<button id=\"upload-btn\" onclick=\"startUpload()\" disabled>Nap Firmware Khong Day</button>"
"<div class=\"progress-bar\" id=\"pbar\"><div class=\"progress-fill\" id=\"pfill\"></div></div>"
"<div id=\"status\"></div>"
"</div><script>"
"var selectedFile=null;"
"function fileChosen(input){if(input.files&&input.files[0]){selectedFile=input.files[0];document.getElementById('filename').innerText=selectedFile.name+' ('+Math.round(selectedFile.size/1024)+' KB)';document.getElementById('upload-btn').disabled=false;}}"
"function startUpload(){if(!selectedFile)return;var btn=document.getElementById('upload-btn');btn.disabled=true;btn.innerText='Dang nap...';"
"var pbar=document.getElementById('pbar');var pfill=document.getElementById('pfill');var stat=document.getElementById('status');pbar.style.display='block';stat.innerText='Dang truyen du lieu...';"
"var xhr=new XMLHttpRequest();xhr.open('POST','/update',true);xhr.setRequestHeader('Content-Type','application/octet-stream');"
"xhr.upload.onprogress=function(e){if(e.lengthComputable){var pct=Math.round((e.loaded/e.total)*100);pfill.style.width=pct+'%';stat.innerText='Dang nap: '+pct+'% ('+Math.round(e.loaded/1024)+'/'+Math.round(e.total/1024)+' KB)';}};"
"xhr.onload=function(){if(xhr.status==200){pfill.style.width='100%';stat.innerText='Thanh cong! ESP32 dang khoi dong lai...';stat.style.color='#a6e3a1';}else{stat.innerText='Loi nap: '+xhr.responseText;stat.style.color='#f38ba8';btn.disabled=false;btn.innerText='Thu lai';}};"
"xhr.onerror=function(){stat.innerText='Loi ket noi den ESP32';stat.style.color='#f38ba8';btn.disabled=false;btn.innerText='Thu lai';};"
"xhr.send(selectedFile);}"
"</script></body></html>";

/* GET /: Serve the HTML page */
static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

/* GET /status: Report device status and current running partition */
static esp_err_t status_get_handler(httpd_req_t *req) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app_desc = esp_app_get_description();
    
    char resp[300];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ready\",\"device\":\"%s\",\"app\":\"%s\",\"version\":\"%s\",\"running_partition\":\"%s\",\"free_heap\":%lu,\"ip\":\"%s\"}",
             WIFI_OTA_HOSTNAME,
             app_desc ? app_desc->project_name : "esp_toy",
             app_desc ? app_desc->version : "1.0",
             running ? running->label : "unknown",
             (unsigned long)esp_get_free_heap_size(),
             s_ip_str);
             
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

/* POST /update: Stream firmware data into next OTA partition */
static esp_err_t update_post_handler(httpd_req_t *req) {
    char ota_buff[2048];
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = NULL;
    
    ESP_LOGI(TAG, "Starting OTA stream receive (Content-Length: %d bytes)...", req->content_len);
    
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No valid OTA update partition found!");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Writing firmware to partition '%s' at offset 0x%lx (size 0x%lx)",
             update_partition->label, update_partition->address, update_partition->size);
             
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        return ESP_FAIL;
    }
    
    int remaining = req->content_len;
    while (remaining > 0) {
        int to_read = MIN(remaining, sizeof(ota_buff));
        int received = httpd_req_recv(req, ota_buff, to_read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "OTA receive error or client disconnected");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive aborted");
            return ESP_FAIL;
        }
        
        err = esp_ota_write(ota_handle, ota_buff, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash write failed");
            return ESP_FAIL;
        }
        remaining -= received;
    }
    
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end validation failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware verification failed");
        return ESP_FAIL;
    }
    
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to switch boot partition");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, ">>> SUCCESS! OTA Update Complete. Rebooting in 500ms...");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK: OTA Update Succeeded. Rebooting...\n", HTTPD_RESP_USE_STRLEN);
    
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* Start the OTA Web Server */
static void start_ota_webserver(void) {
    if (s_http_server) return;
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WIFI_OTA_HTTP_PORT;
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    
    if (httpd_start(&s_http_server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_http_server, &index_uri);
        
        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_http_server, &status_uri);
        
        httpd_uri_t update_uri = {
            .uri = "/update",
            .method = HTTP_POST,
            .handler = update_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(s_http_server, &update_uri);
        
        ESP_LOGI(TAG, "HTTP OTA Server started on port %d", WIFI_OTA_HTTP_PORT);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP OTA Server");
    }
}

/* UDP Broadcast Responder for instant discovery without mDNS */
static void udp_discovery_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: %d", errno);
        vTaskDelete(NULL);
        return;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(WIFI_OTA_UDP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "UDP bind failed: %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "UDP OTA Discovery service listening on port %d", WIFI_OTA_UDP_PORT);
    
    char rx_buffer[128];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (1) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len > 0) {
            rx_buffer[len] = 0;
            if (strstr(rx_buffer, "DISCOVER_ESP32_OTA")) {
                char reply[128];
                snprintf(reply, sizeof(reply), "ESP32_OTA_ACK:%s:%s", WIFI_OTA_HOSTNAME, s_ip_str);
                sendto(sock, reply, strlen(reply), 0, (struct sockaddr *)&client_addr, addr_len);
                ESP_LOGI(TAG, "UDP OTA Discovery replied to %s:%d with IP %s",
                         inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), s_ip_str);
            }
        }
    }
    close(sock);
    vTaskDelete(NULL);
}

/* Wi-Fi & IP Event Handler */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi Station started, connecting to SSID: %s...", CONFIG_WIFI_OTA_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_retry_count++;
        ESP_LOGW(TAG, "Wi-Fi disconnected. Retry #%d in 3s...", s_retry_count);
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        s_retry_count = 0;
        
        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, ">>> [WIFI OTA READY]");
        ESP_LOGI(TAG, ">>> Connected to: %s", CONFIG_WIFI_OTA_SSID);
        ESP_LOGI(TAG, ">>> IP Address  : %s", s_ip_str);
        ESP_LOGI(TAG, ">>> Web OTA URL : http://%s/ (or http://%s.local/)", s_ip_str, WIFI_OTA_HOSTNAME);
        ESP_LOGI(TAG, "==========================================================");
        
        // Start mDNS service
        esp_err_t mdns_err = mdns_init();
        if (mdns_err == ESP_OK) {
            mdns_hostname_set(WIFI_OTA_HOSTNAME);
            mdns_instance_name_set("ESP32-S3 Toy Synthesizer");
            mdns_service_add(NULL, "_http", "_tcp", WIFI_OTA_HTTP_PORT, NULL, 0);
            ESP_LOGI(TAG, "mDNS responder started: %s.local", WIFI_OTA_HOSTNAME);
        } else {
            ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(mdns_err));
        }
        
        // Start Web Server
        start_ota_webserver();
        
        // Start UDP Discovery responder
        if (!s_udp_task_handle) {
            xTaskCreate(udp_discovery_task, "udp_ota_disc", 3072, NULL, 3, &s_udp_task_handle);
        }
    }
}

esp_err_t wifi_ota_init(void) {
    // 1. Initialize NVS (required for Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize TCP/IP and Event Loop
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Check if default event loop already exists
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    esp_netif_create_default_wifi_sta();

    // 3. Initialize Wi-Fi Driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // 4. Set Wi-Fi credentials
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_OTA_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_WIFI_OTA_PASS, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Enable Wi-Fi Modem Sleep to save power while keeping connection active
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
    
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi OTA subsystem initialized in background");
    return ESP_OK;
}

bool wifi_ota_is_connected(void) {
    return s_connected;
}

const char *wifi_ota_get_ip_str(void) {
    return s_ip_str;
}
