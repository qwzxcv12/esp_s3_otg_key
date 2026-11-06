#ifndef MP3_PLAYER_H
#define MP3_PLAYER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MP3_STATE_STOPPED = 0,
    MP3_STATE_PLAYING,
    MP3_STATE_PAUSED
} mp3_player_state_t;

typedef enum {
    REPEAT_MODE_ALL = 0,
    REPEAT_MODE_ONE,
    REPEAT_MODE_SHUFFLE
} repeat_mode_t;

/**
 * @brief Khởi tạo hệ thống phát nhạc MP3 từ USB
 */
esp_err_t mp3_player_init(void);

/**
 * @brief Quét thư mục USB tìm kiếm tất cả các file .mp3 và .wav
 * @param mount_path Đường dẫn mount (ví dụ "/usb")
 * @return Số lượng bài hát tìm thấy
 */
int mp3_player_scan_usb(const char *mount_path);

/**
 * @brief Đánh dấu trạng thái cắm / rút ổ đĩa USB Flash Drive
 */
void mp3_player_set_usb_mounted(bool mounted);
bool mp3_player_is_usb_mounted(void);

/**
 * @brief Xóa toàn bộ danh sách phát khi rút USB
 */
void mp3_player_clear_playlist(void);

/**
 * @brief Lấy tổng số bài hát trong USB
 */
uint16_t mp3_player_get_total_tracks(void);

/**
 * @brief Lấy chỉ mục bài hát đang phát (0, 1, 2...)
 */
uint16_t mp3_player_get_current_index(void);

/**
 * @brief Lấy tên bài hát hiện tại
 */
const char *mp3_player_get_current_filename(void);

/**
 * @brief Lấy trạng thái hiện tại của trình phát nhạc
 */
mp3_player_state_t mp3_player_get_state(void);

/**
 * @brief Bắt đầu phát bài hát theo chỉ mục
 */
esp_err_t mp3_player_play_index(uint16_t index);

/**
 * @brief Tạm dừng hoặc tiếp tục phát bài hát (Toggle)
 */
void mp3_player_toggle_pause(void);

/**
 * @brief Chuyển sang bài hát kế tiếp (Next Track)
 */
esp_err_t mp3_player_next(void);

/**
 * @brief Lùi về bài hát trước đó (Previous Track)
 */
esp_err_t mp3_player_prev(void);

/**
 * @brief Dừng phát nhạc
 */
esp_err_t mp3_player_stop(void);

/**
 * @brief Dừng phát nhạc đồng bộ (chờ đóng file an toàn trước khi unmount USB)
 */
void mp3_player_stop_sync(void);

/**
 * @brief Đổi chế độ lặp bài (All, One, Shuffle)
 */
void mp3_player_set_repeat_mode(repeat_mode_t mode);
repeat_mode_t mp3_player_get_repeat_mode(void);
const char *mp3_player_get_repeat_mode_name(void);

#ifdef __cplusplus
}
#endif

#endif // MP3_PLAYER_H
