#include "mp3_player.h"
#include "audio_synth.h"
#include "led_effects.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static const char *TAG = "MP3_PLAYER";

#define MAX_TRACKS 512
#define FILE_READ_BUF_SIZE (16 * 1024)

typedef struct {
    char path[256];
    char filename[96];
} track_item_t;

typedef enum {
    CMD_PLAY_INDEX,
    CMD_TOGGLE_PAUSE,
    CMD_NEXT,
    CMD_PREV,
    CMD_STOP
} player_cmd_type_t;

typedef struct {
    player_cmd_type_t type;
    uint16_t index;
} player_cmd_t;

// State variables
static track_item_t *s_playlist = NULL;
static uint16_t s_total_tracks = 0;
static uint16_t s_current_index = 0;
static mp3_player_state_t s_state = MP3_STATE_STOPPED;
static repeat_mode_t s_repeat_mode = REPEAT_MODE_ALL;

static QueueHandle_t s_cmd_queue = NULL;
static TaskHandle_t s_player_task_handle = NULL;
static SemaphoreHandle_t s_player_mutex = NULL;

static bool is_audio_file(const char *name) {
    size_t len = strlen(name);
    if (len < 5) return false;
    const char *ext = name + len - 4;
    return (strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".wav") == 0);
}

static void scan_directory_recursive(const char *dir_path) {
    if (s_total_tracks >= MAX_TRACKS) return;

    DIR *d = opendir(dir_path);
    if (!d) {
        ESP_LOGW(TAG, "Cannot open directory: %s", dir_path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && s_total_tracks < MAX_TRACKS) {
        if (entry->d_name[0] == '.') continue; // Skip hidden / . and ..

        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_directory_recursive(full_path);
            } else if (S_ISREG(st.st_mode) && is_audio_file(entry->d_name)) {
                strncpy(s_playlist[s_total_tracks].path, full_path, sizeof(s_playlist[s_total_tracks].path) - 1);
                strncpy(s_playlist[s_total_tracks].filename, entry->d_name, sizeof(s_playlist[s_total_tracks].filename) - 1);
                s_total_tracks++;
            }
        }
    }
    closedir(d);
}

int mp3_player_scan_usb(const char *mount_path) {
    if (!s_playlist) {
        s_playlist = (track_item_t *)heap_caps_malloc(MAX_TRACKS * sizeof(track_item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_playlist) {
            s_playlist = (track_item_t *)malloc(MAX_TRACKS * sizeof(track_item_t));
        }
        if (!s_playlist) {
            ESP_LOGE(TAG, "Failed to allocate memory for playlist!");
            return 0;
        }
    }

    if (xSemaphoreTake(s_player_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_total_tracks = 0;
        s_current_index = 0;
        scan_directory_recursive(mount_path);
        ESP_LOGI(TAG, "Scanned %s -> Found %d audio tracks", mount_path, s_total_tracks);
        xSemaphoreGive(s_player_mutex);
    }

    return s_total_tracks;
}

void mp3_player_clear_playlist(void) {
    mp3_player_stop();
    if (xSemaphoreTake(s_player_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_total_tracks = 0;
        s_current_index = 0;
        s_state = MP3_STATE_STOPPED;
        xSemaphoreGive(s_player_mutex);
    }
}

uint16_t mp3_player_get_total_tracks(void) {
    return s_total_tracks;
}

uint16_t mp3_player_get_current_index(void) {
    return s_current_index;
}

const char *mp3_player_get_current_filename(void) {
    if (s_total_tracks == 0 || s_current_index >= s_total_tracks) return "Không có bài hát";
    return s_playlist[s_current_index].filename;
}

mp3_player_state_t mp3_player_get_state(void) {
    return s_state;
}

void mp3_player_set_repeat_mode(repeat_mode_t mode) {
    s_repeat_mode = mode;
}

repeat_mode_t mp3_player_get_repeat_mode(void) {
    return s_repeat_mode;
}

const char *mp3_player_get_repeat_mode_name(void) {
    switch (s_repeat_mode) {
        case REPEAT_MODE_ALL: return "Lặp Lại Toàn Bộ";
        case REPEAT_MODE_ONE: return "Lặp Lại 1 Bài";
        case REPEAT_MODE_SHUFFLE: return "Phát Ngẫu Nhiên";
        default: return "Toàn bộ";
    }
}

static bool play_wav_file(FILE *f, player_cmd_t *cmd_out) {
    uint8_t header[44];
    if (fread(header, 1, 44, f) != 44) return false;

    // Basic WAV validation
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t channels = header[22] | (header[23] << 8);
    uint32_t sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint16_t bits = header[34] | (header[35] << 8);

    if (bits != 16) {
        ESP_LOGW(TAG, "Unsupported WAV bit depth: %d (only 16-bit supported)", bits);
        return false;
    }

    audio_stream_start(sample_rate, channels);

    int16_t *buf = (int16_t *)heap_caps_malloc(2048 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (int16_t *)malloc(2048 * sizeof(int16_t));
    if (!buf) return false;

    bool finished = true;
    while (!feof(f)) {
        if (xQueueReceive(s_cmd_queue, cmd_out, 0) == pdTRUE) {
            finished = false;
            break;
        }

        if (s_state == MP3_STATE_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        size_t read_bytes = fread(buf, 1, 2048 * sizeof(int16_t), f);
        if (read_bytes == 0) break;

        size_t samples = read_bytes / sizeof(int16_t);
        audio_stream_write(buf, samples);

        float eng = audio_stream_get_energy();
        if (eng > 0.05f) {
            led_effects_music_energy(eng);
        }
    }

    free(buf);
    return finished;
}

static bool play_mp3_file(FILE *f, player_cmd_t *cmd_out) {
    uint8_t *read_buf = (uint8_t *)heap_caps_malloc(FILE_READ_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!read_buf) read_buf = (uint8_t *)malloc(FILE_READ_BUF_SIZE);
    if (!read_buf) return false;

    short *pcm = (short *)heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(short), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm) pcm = (short *)malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(short));
    if (!pcm) {
        free(read_buf);
        return false;
    }

    mp3dec_t mp3d;
    mp3dec_init(&mp3d);
    mp3dec_frame_info_t info;

    int bytes_in_buf = 0;
    bool stream_started = false;
    bool finished = true;

    while (1) {
        if (xQueueReceive(s_cmd_queue, cmd_out, 0) == pdTRUE) {
            finished = false;
            break;
        }

        if (s_state == MP3_STATE_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Refill buffer if low on data
        if (bytes_in_buf < 4096 && !feof(f)) {
            size_t bytes_to_read = FILE_READ_BUF_SIZE - bytes_in_buf;
            size_t read_bytes = fread(read_buf + bytes_in_buf, 1, bytes_to_read, f);
            bytes_in_buf += read_bytes;
        }

        if (bytes_in_buf <= 0) break; // Finished file

        int samples = mp3dec_decode_frame(&mp3d, read_buf, bytes_in_buf, pcm, &info);

        if (info.frame_bytes > 0) {
            bytes_in_buf -= info.frame_bytes;
            if (bytes_in_buf > 0) {
                memmove(read_buf, read_buf + info.frame_bytes, bytes_in_buf);
            }
        } else {
            // No valid sync found or need more data
            if (feof(f)) break;
            bytes_in_buf = 0;
            continue;
        }

        if (samples > 0) {
            if (!stream_started) {
                audio_stream_start(info.hz, info.channels);
                stream_started = true;
            }

            audio_stream_write(pcm, samples * info.channels);

            float eng = audio_stream_get_energy();
            if (eng > 0.05f) {
                led_effects_music_energy(eng);
            }
        }
    }

    free(read_buf);
    free(pcm);
    return finished;
}

static void mp3_player_task(void *arg) {
    player_cmd_t cmd;

    while (1) {
        if (s_state != MP3_STATE_PLAYING) {
            if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
                if (cmd.type == CMD_PLAY_INDEX) {
                    s_current_index = cmd.index;
                    s_state = MP3_STATE_PLAYING;
                } else if (cmd.type == CMD_TOGGLE_PAUSE) {
                    s_state = (s_state == MP3_STATE_PAUSED) ? MP3_STATE_PLAYING : MP3_STATE_PAUSED;
                }
            }
            continue;
        }

        if (s_total_tracks == 0) {
            s_state = MP3_STATE_STOPPED;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const char *file_path = s_playlist[s_current_index].path;
        ESP_LOGI(TAG, "Playing [%d/%d]: %s", s_current_index + 1, s_total_tracks, s_playlist[s_current_index].filename);

        FILE *f = fopen(file_path, "rb");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open audio file: %s", file_path);
            // Skip to next file
            s_current_index = (s_current_index + 1) % s_total_tracks;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        bool is_wav = (strcasecmp(file_path + strlen(file_path) - 4, ".wav") == 0);
        player_cmd_t next_cmd;
        memset(&next_cmd, 0, sizeof(next_cmd));

        bool natural_finish = false;
        if (is_wav) {
            natural_finish = play_wav_file(f, &next_cmd);
        } else {
            natural_finish = play_mp3_file(f, &next_cmd);
        }

        fclose(f);
        audio_stream_stop();

        if (natural_finish) {
            // Finished track naturally -> choose next track
            if (s_repeat_mode == REPEAT_MODE_ONE) {
                // Stay on current track
            } else if (s_repeat_mode == REPEAT_MODE_SHUFFLE) {
                s_current_index = esp_random() % s_total_tracks;
            } else {
                s_current_index = (s_current_index + 1) % s_total_tracks;
            }
            vTaskDelay(pdMS_TO_TICKS(200)); // Small gap between songs
        } else {
            // Process command that interrupted playback
            switch (next_cmd.type) {
                case CMD_PLAY_INDEX:
                    s_current_index = next_cmd.index;
                    s_state = MP3_STATE_PLAYING;
                    break;
                case CMD_NEXT:
                    s_current_index = (s_current_index + 1) % s_total_tracks;
                    s_state = MP3_STATE_PLAYING;
                    break;
                case CMD_PREV:
                    s_current_index = (s_current_index + s_total_tracks - 1) % s_total_tracks;
                    s_state = MP3_STATE_PLAYING;
                    break;
                case CMD_TOGGLE_PAUSE:
                    s_state = (s_state == MP3_STATE_PLAYING) ? MP3_STATE_PAUSED : MP3_STATE_PLAYING;
                    break;
                case CMD_STOP:
                    s_state = MP3_STATE_STOPPED;
                    break;
            }
        }
    }
}

esp_err_t mp3_player_init(void) {
    if (s_player_mutex == NULL) {
        s_player_mutex = xSemaphoreCreateMutex();
    }
    if (s_cmd_queue == NULL) {
        s_cmd_queue = xQueueCreate(8, sizeof(player_cmd_t));
    }
    if (s_player_task_handle == NULL) {
        xTaskCreatePinnedToCore(mp3_player_task, "mp3_task", 8192, NULL, 5, &s_player_task_handle, 1);
    }
    ESP_LOGI(TAG, "MP3 Player Subsystem initialized");
    return ESP_OK;
}

esp_err_t mp3_player_play_index(uint16_t index) {
    if (s_total_tracks == 0) return ESP_ERR_NOT_FOUND;
    player_cmd_t cmd = {
        .type = CMD_PLAY_INDEX,
        .index = index % s_total_tracks
    };
    xQueueSend(s_cmd_queue, &cmd, 0);
    return ESP_OK;
}

void mp3_player_toggle_pause(void) {
    player_cmd_t cmd = {
        .type = CMD_TOGGLE_PAUSE
    };
    xQueueSend(s_cmd_queue, &cmd, 0);
}

esp_err_t mp3_player_next(void) {
    if (s_total_tracks == 0) return ESP_ERR_NOT_FOUND;
    player_cmd_t cmd = {
        .type = CMD_NEXT
    };
    xQueueSend(s_cmd_queue, &cmd, 0);
    return ESP_OK;
}

esp_err_t mp3_player_prev(void) {
    if (s_total_tracks == 0) return ESP_ERR_NOT_FOUND;
    player_cmd_t cmd = {
        .type = CMD_PREV
    };
    xQueueSend(s_cmd_queue, &cmd, 0);
    return ESP_OK;
}

esp_err_t mp3_player_stop(void) {
    player_cmd_t cmd = {
        .type = CMD_STOP
    };
    xQueueSend(s_cmd_queue, &cmd, 0);
    audio_stream_stop();
    return ESP_OK;
}
