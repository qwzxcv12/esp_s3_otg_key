#include "soundbank.h"
#include "audio_synth.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_littlefs.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "SOUNDBANK";

#define MOUNT_PATH      "/storage"
#define MAX_WAV_BYTES   (3 * 1024 * 1024)
#define M(n)            (1u << (n))      // mode index bit (0 = Piano ... 8 = Phonics)
#define ALWAYS          0x8000u          // loaded in every mode

typedef enum {
    SND_CHIME, SND_COIN,
    SND_MODE_1, SND_MODE_2, SND_MODE_3, SND_MODE_4, SND_MODE_5,
    SND_MODE_6, SND_MODE_7, SND_MODE_8, SND_MODE_9,
    SND_PIANO_C2, SND_PIANO_FS2, SND_PIANO_C3, SND_PIANO_FS3,
    SND_PIANO_C4, SND_PIANO_D4, SND_PIANO_E4, SND_PIANO_F4, SND_PIANO_G4, SND_PIANO_A4, SND_PIANO_B4,
    SND_PIANO_C5, SND_PIANO_FS5, SND_PIANO_C6, SND_PIANO_C7,
    SND_BELL_C4, SND_BELL_C5, SND_BELL_C6, SND_BELL_C7,
    SND_KICK, SND_SNARE, SND_HIHAT, SND_CYMBAL, SND_TOM, SND_CLAP,
    SND_TW_CLICK, SND_TW_SPACE, SND_TW_BELL, SND_TW_BACK,
    SND_CT_BOING, SND_CT_LASER, SND_CT_HORN, SND_CT_FANFARE, SND_CT_WHISTLE,
    SND_CT_DROP, SND_CT_DOORBELL, SND_CT_SIREN, SND_CT_SKID,
    SND_CT_COIN_M6,
    SND_CT_JUMP, SND_CT_POWERUP, SND_CT_1UP, SND_CT_FIREBALL, SND_CT_WARP,
    SND_CT_BOMB, SND_CT_PACMAN, SND_CT_SONIC, SND_CT_PUNCH, SND_CT_SAD_TROMBONE,
    SND_CT_BONGO_RUN, SND_CT_SLIP_FALL, SND_CT_ANVIL, SND_CT_SLAPSTICK,
    SND_CT_FLEXATONE, SND_CT_VIBRASLAP, SND_CT_SQUEAK_DUCK, SND_CT_SPLAT,
    SND_CT_BALL_WHISTLE, SND_CT_COWBELL, SND_CT_MAGIC_WAND, SND_CT_GHOST,
    SND_CT_TIMPANI, SND_CT_RUN_AWAY,
    // Mode 8: Cockpit SFX & 26 Animals
    SND_COCKPIT_SIREN,
    SND_COCKPIT_HORN,
    SND_COCKPIT_ENGINE,
    SND_COCKPIT_WATER,
    SND_COCKPIT_FIRE,
    SND_COCKPIT_HELI,
    SND_COCKPIT_CHEER,
    SND_COCKPIT_BELL,
    SND_COCKPIT_SKID,
    SND_COCKPIT_ROCKET,
    SND_COCKPIT_REVERSE,
    SND_COCKPIT_TURN,
    SND_COCKPIT_RADIO,
    SND_COCKPIT_MISSION,
    SND_COCKPIT_SUCCESS,
    SND_M8_ANIMAL_A,
    SND_M8_ANIMAL_Z = SND_M8_ANIMAL_A + 25,
    SND_LETTER_A,                       // 26 letters follow
    SND_NUM_0 = SND_LETTER_A + 26,      // 11 numbers follow (0..10)
    SND_PRAISE = SND_NUM_0 + 11,

    // Phím chức năng, điều hướng & tổ hợp
    SND_KEY_SPACE, SND_KEY_ENTER, SND_KEY_BACKSPACE, SND_KEY_TAB,
    SND_KEY_CAPSLOCK, SND_KEY_SHIFT, SND_KEY_CTRL, SND_KEY_ALT,
    SND_KEY_WIN, SND_KEY_ESC, SND_KEY_DELETE, SND_KEY_INSERT,
    SND_KEY_HOME, SND_KEY_END, SND_KEY_PAGEUP, SND_KEY_PAGEDOWN,
    SND_KEY_ARROW_UP, SND_KEY_ARROW_DOWN, SND_KEY_ARROW_LEFT, SND_KEY_ARROW_RIGHT,
    SND_KEY_NUMLOCK, SND_KEY_SCROLLLOCK, SND_KEY_PAUSE, SND_KEY_PRTSCR,
    SND_KEY_MENU,
    SND_KEY_F1, SND_KEY_F2, SND_KEY_F3, SND_KEY_F4, SND_KEY_F5, SND_KEY_F6,
    SND_KEY_F7, SND_KEY_F8, SND_KEY_F9, SND_KEY_F10, SND_KEY_F11, SND_KEY_F12,

    // Ký tự, dấu câu & phép toán
    SND_SYM_PLUS, SND_SYM_MINUS, SND_SYM_MULTIPLY, SND_SYM_DIVIDE, SND_SYM_EQUAL,
    SND_SYM_DOT, SND_SYM_COMMA, SND_SYM_SEMICOLON, SND_SYM_COLON,
    SND_SYM_EXCLAIM, SND_SYM_QUESTION, SND_SYM_QUOTE_SINGLE, SND_SYM_QUOTE_DOUBLE,
    SND_SYM_AT, SND_SYM_HASH, SND_SYM_DOLLAR, SND_SYM_PERCENT, SND_SYM_CARET,
    SND_SYM_AMPERSAND, SND_SYM_PAREN_OPEN, SND_SYM_PAREN_CLOSE,
    SND_SYM_BRACKET_OPEN, SND_SYM_BRACKET_CLOSE,
    SND_SYM_BRACE_OPEN, SND_SYM_BRACE_CLOSE,
    SND_SYM_LESS_THAN, SND_SYM_GREATER_THAN,
    SND_SYM_BACKSLASH, SND_SYM_PIPE, SND_SYM_TILDE, SND_SYM_BACKTICK, SND_SYM_UNDERSCORE,

    SND_COUNT
} snd_id_t;

typedef struct {
    char path[96];
    uint16_t modes;        // bitmask of modes that need this file (or ALWAYS)
    pcm_asset_t asset;
} snd_entry_t;

static snd_entry_t s_snd[SND_COUNT];
static SemaphoreHandle_t s_bank_mutex = NULL;   // protects asset pointers vs. loader frees
static TaskHandle_t s_loader_task = NULL;
static volatile uint8_t s_target_mode = 0;
static bool s_mounted = false;

static void reg(snd_id_t id, const char *rel_path, uint16_t modes) {
    snprintf(s_snd[id].path, sizeof(s_snd[id].path), MOUNT_PATH "/%s", rel_path);
    s_snd[id].modes = modes;
}

static void build_table(void) {
    memset(s_snd, 0, sizeof(s_snd));
    reg(SND_CHIME, "common/chime.wav", ALWAYS);
    reg(SND_COIN,  "common/coin.wav",  ALWAYS);

    for (int i = 0; i < 9; i++) {
        char p[40];
        snprintf(p, sizeof(p), "common/mode_%d.wav", i + 1);
        reg((snd_id_t)(SND_MODE_1 + i), p, ALWAYS);
    }

    static const char *piano_names[15] = {
        "c2", "fs2", "c3", "fs3",
        "c4", "d4", "e4", "f4", "g4", "a4", "b4",
        "c5", "fs5", "c6", "c7"
    };
    for (int i = 0; i < 15; i++) {
        char p[40];
        snprintf(p, sizeof(p), "mode1/piano_%s.wav", piano_names[i]);
        reg((snd_id_t)(SND_PIANO_C2 + i), p, M(0) | M(1) | M(2));   // Piano + Baby Mozart + Speed Melody
    }
    reg(SND_BELL_C4, "mode1/bell_c4.wav", M(0) | M(1) | M(2) | M(8));
    reg(SND_BELL_C5, "mode1/bell_c5.wav", M(0) | M(1) | M(2) | M(8));
    reg(SND_BELL_C6, "mode1/bell_c6.wav", M(0) | M(1) | M(2) | M(8));
    reg(SND_BELL_C7, "mode1/bell_c7.wav", M(0) | M(1) | M(2) | M(8));

    reg(SND_KICK,   "mode4/kick.wav",   M(3));
    reg(SND_SNARE,  "mode4/snare.wav",  M(3));
    reg(SND_HIHAT,  "mode4/hihat.wav",  M(3));
    reg(SND_CYMBAL, "mode4/cymbal.wav", M(3));
    reg(SND_TOM,    "mode4/tom.wav",    M(3) | M(8));         // Mode 9 uses it for counting beats
    reg(SND_CLAP,   "mode4/clap.wav",   M(3));

    reg(SND_TW_CLICK, "mode5/key_click.wav", M(4));
    reg(SND_TW_SPACE, "mode5/space.wav",     M(4));
    reg(SND_TW_BELL,  "mode5/bell.wav",      M(4));
    reg(SND_TW_BACK,  "mode5/backspace.wav", M(4) | M(8));

    reg(SND_CT_BOING,         "mode6/boing.wav",         ALWAYS);
    reg(SND_CT_LASER,         "mode6/laser.wav",         M(5) | M(7));
    reg(SND_CT_HORN,          "mode6/horn.wav",          M(5) | M(7));
    reg(SND_CT_FANFARE,       "mode6/fanfare.wav",       M(1) | M(2) | M(5) | M(8));
    reg(SND_CT_WHISTLE,       "mode6/whistle.wav",       M(5));
    reg(SND_CT_DROP,          "mode6/water_drop.wav",    M(5) | M(8));
    reg(SND_CT_DOORBELL,      "mode6/doorbell.wav",      M(5));
    reg(SND_CT_SIREN,         "mode6/siren.wav",         M(5) | M(7));
    reg(SND_CT_SKID,          "mode6/skid.wav",          M(5) | M(7));
    reg(SND_CT_COIN_M6,       "mode6/coin.wav",          M(5));
    reg(SND_CT_JUMP,          "mode6/jump.wav",          M(5));
    reg(SND_CT_POWERUP,       "mode6/powerup.wav",       M(5));
    reg(SND_CT_1UP,           "mode6/1up.wav",           M(5));
    reg(SND_CT_FIREBALL,      "mode6/fireball.wav",      M(5));
    reg(SND_CT_WARP,          "mode6/warp.wav",          M(5));
    reg(SND_CT_BOMB,          "mode6/bomb.wav",          M(5));
    reg(SND_CT_PACMAN,        "mode6/pacman.wav",        M(5));
    reg(SND_CT_SONIC,         "mode6/sonic.wav",         M(5));
    reg(SND_CT_PUNCH,         "mode6/punch.wav",         M(5));
    reg(SND_CT_SAD_TROMBONE,  "mode6/sad_trombone.wav",  M(5));
    reg(SND_CT_BONGO_RUN,     "mode6/bongo_run.wav",     M(5));
    reg(SND_CT_SLIP_FALL,     "mode6/slip_fall.wav",     M(5));
    reg(SND_CT_ANVIL,         "mode6/anvil.wav",         M(5));
    reg(SND_CT_SLAPSTICK,     "mode6/slapstick.wav",     M(5));
    reg(SND_CT_FLEXATONE,     "mode6/flexatone.wav",     M(5));
    reg(SND_CT_VIBRASLAP,     "mode6/vibraslap.wav",     M(5));
    reg(SND_CT_SQUEAK_DUCK,   "mode6/squeak_duck.wav",   M(5));
    reg(SND_CT_SPLAT,         "mode6/splat.wav",         M(5));
    reg(SND_CT_BALL_WHISTLE,  "mode6/ball_whistle.wav",  M(5));
    reg(SND_CT_COWBELL,       "mode6/cowbell.wav",       M(5));
    reg(SND_CT_MAGIC_WAND,    "mode6/magic_wand.wav",    M(5));
    reg(SND_CT_GHOST,         "mode6/ghost.wav",         M(5));
    reg(SND_CT_TIMPANI,       "mode6/timpani.wav",       M(5));
    reg(SND_CT_RUN_AWAY,      "mode6/run_away.wav",      M(5));

    // Mode 8: Cockpit & 26 Animals
    reg(SND_COCKPIT_SIREN,   "mode8/siren.wav",      M(7));
    reg(SND_COCKPIT_HORN,    "mode8/horn.wav",       M(7));
    reg(SND_COCKPIT_ENGINE,  "mode8/engine.wav",     M(7));
    reg(SND_COCKPIT_WATER,   "mode8/water.wav",      M(7));
    reg(SND_COCKPIT_FIRE,    "mode8/fire.wav",       M(7));
    reg(SND_COCKPIT_HELI,    "mode8/helicopter.wav", M(7));
    reg(SND_COCKPIT_CHEER,   "mode8/cheer.wav",      M(7));
    reg(SND_COCKPIT_BELL,    "mode8/bell.wav",       M(7));
    reg(SND_COCKPIT_SKID,    "mode8/skid.wav",       M(7));
    reg(SND_COCKPIT_ROCKET,  "mode8/rocket.wav",     M(7));
    reg(SND_COCKPIT_REVERSE, "mode8/reverse.wav",    M(7));
    reg(SND_COCKPIT_TURN,    "mode8/turn.wav",       M(7));
    reg(SND_COCKPIT_RADIO,   "mode8/radio.wav",      M(7));
    reg(SND_COCKPIT_MISSION, "mode8/mission.wav",    M(7));
    reg(SND_COCKPIT_SUCCESS, "mode8/success.wav",    M(7));

    for (int i = 0; i < 26; i++) {
        char p[40];
        snprintf(p, sizeof(p), "mode8/animal_%c.wav", 'a' + i);
        reg((snd_id_t)(SND_M8_ANIMAL_A + i), p, M(7));
    }

    for (int i = 0; i < 26; i++) {
        char p[40];
        snprintf(p, sizeof(p), "mode9/letter_%c.wav", 'a' + i);
        reg((snd_id_t)(SND_LETTER_A + i), p, M(8));
    }
    for (int i = 0; i <= 10; i++) {
        char p[40];
        snprintf(p, sizeof(p), "mode9/num_%d.wav", i);
        reg((snd_id_t)(SND_NUM_0 + i), p, M(8));
    }
    reg(SND_PRAISE, "mode9/praise.wav", M(1) | M(2) | M(8));

    // Phím chức năng & điều hướng
    reg(SND_KEY_SPACE,      "mode9/key_space.wav",      M(8));
    reg(SND_KEY_ENTER,      "mode9/key_enter.wav",      M(8));
    reg(SND_KEY_BACKSPACE,  "mode9/key_backspace.wav",  M(8));
    reg(SND_KEY_TAB,        "mode9/key_tab.wav",        M(8));
    reg(SND_KEY_CAPSLOCK,   "mode9/key_capslock.wav",   M(8));
    reg(SND_KEY_SHIFT,      "mode9/key_shift.wav",      M(8));
    reg(SND_KEY_CTRL,       "mode9/key_ctrl.wav",       M(8));
    reg(SND_KEY_ALT,        "mode9/key_alt.wav",        M(8));
    reg(SND_KEY_WIN,        "mode9/key_win.wav",        M(8));
    reg(SND_KEY_ESC,        "mode9/key_esc.wav",        M(8));
    reg(SND_KEY_DELETE,     "mode9/key_delete.wav",     M(8));
    reg(SND_KEY_INSERT,     "mode9/key_insert.wav",     M(8));
    reg(SND_KEY_HOME,       "mode9/key_home.wav",       M(8));
    reg(SND_KEY_END,        "mode9/key_end.wav",        M(8));
    reg(SND_KEY_PAGEUP,     "mode9/key_pageup.wav",     M(8));
    reg(SND_KEY_PAGEDOWN,   "mode9/key_pagedown.wav",   M(8));
    reg(SND_KEY_ARROW_UP,   "mode9/key_arrow_up.wav",   M(8));
    reg(SND_KEY_ARROW_DOWN, "mode9/key_arrow_down.wav", M(8));
    reg(SND_KEY_ARROW_LEFT, "mode9/key_arrow_left.wav", M(8));
    reg(SND_KEY_ARROW_RIGHT,"mode9/key_arrow_right.wav",M(8));
    reg(SND_KEY_NUMLOCK,    "mode9/key_numlock.wav",    M(8));
    reg(SND_KEY_SCROLLLOCK, "mode9/key_scrolllock.wav", M(8));
    reg(SND_KEY_PAUSE,      "mode9/key_pause.wav",      M(8));
    reg(SND_KEY_PRTSCR,     "mode9/key_prtscr.wav",     M(8));
    reg(SND_KEY_MENU,       "mode9/key_menu.wav",       M(8));
    for (int i = 1; i <= 12; i++) {
        char p[32];
        snprintf(p, sizeof(p), "mode9/key_f%d.wav", i);
        reg((snd_id_t)(SND_KEY_F1 + i - 1), p, M(8));
    }

    // Ký tự, dấu câu & phép toán
    reg(SND_SYM_PLUS,         "mode9/sym_plus.wav",         M(8));
    reg(SND_SYM_MINUS,        "mode9/sym_minus.wav",        M(8));
    reg(SND_SYM_MULTIPLY,     "mode9/sym_multiply.wav",     M(8));
    reg(SND_SYM_DIVIDE,       "mode9/sym_divide.wav",       M(8));
    reg(SND_SYM_EQUAL,        "mode9/sym_equal.wav",        M(8));
    reg(SND_SYM_DOT,          "mode9/sym_dot.wav",          M(8));
    reg(SND_SYM_COMMA,        "mode9/sym_comma.wav",        M(8));
    reg(SND_SYM_SEMICOLON,    "mode9/sym_semicolon.wav",    M(8));
    reg(SND_SYM_COLON,        "mode9/sym_colon.wav",        M(8));
    reg(SND_SYM_EXCLAIM,      "mode9/sym_exclaim.wav",      M(8));
    reg(SND_SYM_QUESTION,     "mode9/sym_question.wav",     M(8));
    reg(SND_SYM_QUOTE_SINGLE, "mode9/sym_quote_single.wav", M(8));
    reg(SND_SYM_QUOTE_DOUBLE, "mode9/sym_quote_double.wav", M(8));
    reg(SND_SYM_AT,           "mode9/sym_at.wav",           M(8));
    reg(SND_SYM_HASH,         "mode9/sym_hash.wav",         M(8));
    reg(SND_SYM_DOLLAR,       "mode9/sym_dollar.wav",       M(8));
    reg(SND_SYM_PERCENT,      "mode9/sym_percent.wav",      M(8));
    reg(SND_SYM_CARET,        "mode9/sym_caret.wav",        M(8));
    reg(SND_SYM_AMPERSAND,    "mode9/sym_ampersand.wav",    M(8));
    reg(SND_SYM_PAREN_OPEN,   "mode9/sym_paren_open.wav",   M(8));
    reg(SND_SYM_PAREN_CLOSE,  "mode9/sym_paren_close.wav",  M(8));
    reg(SND_SYM_BRACKET_OPEN, "mode9/sym_bracket_open.wav", M(8));
    reg(SND_SYM_BRACKET_CLOSE,"mode9/sym_bracket_close.wav",M(8));
    reg(SND_SYM_BRACE_OPEN,   "mode9/sym_brace_open.wav",   M(8));
    reg(SND_SYM_BRACE_CLOSE,  "mode9/sym_brace_close.wav",  M(8));
    reg(SND_SYM_LESS_THAN,    "mode9/sym_less_than.wav",    M(8));
    reg(SND_SYM_GREATER_THAN, "mode9/sym_greater_than.wav", M(8));
    reg(SND_SYM_BACKSLASH,    "mode9/sym_backslash.wav",    M(8));
    reg(SND_SYM_PIPE,         "mode9/sym_pipe.wav",         M(8));
    reg(SND_SYM_TILDE,        "mode9/sym_tilde.wav",        M(8));
    reg(SND_SYM_BACKTICK,     "mode9/sym_backtick.wav",     M(8));
    reg(SND_SYM_UNDERSCORE,   "mode9/sym_underscore.wav",   M(8));
}

// ---------------------------------------------------------------------------
// WAV loader: walks RIFF chunks, skips LIST/JUNK/etc, requires PCM 16-bit mono
// ---------------------------------------------------------------------------
static esp_err_t load_wav(const char *path, pcm_asset_t *out) {
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    uint8_t riff[12];
    if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(f);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint16_t fmt_tag = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    esp_err_t ret = ESP_FAIL;
    char id[4];
    uint32_t size = 0;

    while (fread(id, 1, 4, f) == 4 && fread(&size, 4, 1, f) == 1) {
        if (memcmp(id, "fmt ", 4) == 0) {
            uint8_t b[16] = {0};
            size_t n = (size < sizeof(b)) ? size : sizeof(b);
            if (fread(b, 1, n, f) != n) break;
            if (size > n) fseek(f, (long)(size - n), SEEK_CUR);
            fmt_tag  = b[0] | (b[1] << 8);
            channels = b[2] | (b[3] << 8);
            rate     = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
            bits     = b[14] | (b[15] << 8);
            if (size & 1) fseek(f, 1, SEEK_CUR);
        } else if (memcmp(id, "data", 4) == 0) {
            if (fmt_tag != 1 || channels != 1 || bits != 16 || rate == 0) {
                ESP_LOGE(TAG, "%s: need PCM16 mono (got fmt=%u ch=%u bits=%u)", path, fmt_tag, channels, bits);
                ret = ESP_ERR_NOT_SUPPORTED;
                break;
            }
            if (size > MAX_WAV_BYTES) size = MAX_WAV_BYTES;
            int16_t *buf = (int16_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buf) {
                ESP_LOGE(TAG, "%s: PSRAM alloc of %lu bytes failed", path, (unsigned long)size);
                ret = ESP_ERR_NO_MEM;
                break;
            }
            size_t got = fread(buf, 1, size, f);
            if (got < 4) {
                heap_caps_free(buf);
                break;
            }
            out->pcm = buf;
            out->num_samples = (uint32_t)(got / sizeof(int16_t));
            out->sample_rate = rate;
            ret = ESP_OK;
            break;
        } else {
            fseek(f, (long)(size + (size & 1)), SEEK_CUR);
        }
    }
    fclose(f);
    return ret;
}

// ---------------------------------------------------------------------------
// Bank management (runs in its own low-priority task so USB/HID never stalls)
// ---------------------------------------------------------------------------
static bool needed(const snd_entry_t *e, uint8_t mode) {
    return (e->modes & ALWAYS) || (e->modes & M(mode));
}

static void bank_load_for_mode(uint8_t mode) {
    int64_t t0 = esp_timer_get_time();
    int freed = 0, loaded = 0, missing = 0;

    // 1. Free assets that the new mode does not need (stops only the voices that use them)
    for (int i = 0; i < SND_COUNT; i++) {
        snd_entry_t *e = &s_snd[i];
        if (e->asset.pcm && !needed(e, mode)) {
            xSemaphoreTake(s_bank_mutex, portMAX_DELAY);
            int16_t *p = e->asset.pcm;
            e->asset.pcm = NULL;
            e->asset.num_samples = 0;
            audio_sample_invalidate(p);
            heap_caps_free(p);
            xSemaphoreGive(s_bank_mutex);
            freed++;
        }
    }

    // 2. Load what is needed and not loaded yet
    for (int i = 0; i < SND_COUNT; i++) {
        if (mode != s_target_mode) return;          // user already switched again: restart
        snd_entry_t *e = &s_snd[i];
        if (!needed(e, mode) || e->asset.pcm) continue;
        pcm_asset_t a;
        if (load_wav(e->path, &a) == ESP_OK) {
            xSemaphoreTake(s_bank_mutex, portMAX_DELAY);
            e->asset = a;
            xSemaphoreGive(s_bank_mutex);
            loaded++;
        } else {
            missing++;
        }
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield CPU so USB Host & audio tasks never stall
    }

    ESP_LOGI(TAG, "Mode %d bank ready: +%d loaded, -%d freed, %d missing files, %lld ms, PSRAM free %u KB",
             mode + 1, loaded, freed, missing, (long long)((esp_timer_get_time() - t0) / 1000),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static void loader_task(void *arg) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint8_t mode;
        do {
            mode = s_target_mode;
            bank_load_for_mode(mode);
        } while (mode != s_target_mode);
    }
}

esp_err_t soundbank_init(void) {
    build_table();
    s_bank_mutex = xSemaphoreCreateMutex();

    esp_vfs_littlefs_conf_t conf = {
        .base_path = MOUNT_PATH,
        .partition_label = "storage",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS mount failed (%s) - using built-in synth only", esp_err_to_name(ret));
        return ret;
    }
    s_mounted = true;

    size_t total = 0, used = 0;
    esp_littlefs_info("storage", &total, &used);
    ESP_LOGI(TAG, "LittleFS mounted: %u KB used of %u KB", (unsigned)(used / 1024), (unsigned)(total / 1024));

    // Preload ALWAYS assets synchronously on startup so first mode announcement plays immediately
    for (int i = 0; i <= SND_MODE_9; i++) {
        pcm_asset_t a;
        if (load_wav(s_snd[i].path, &a) == ESP_OK) {
            s_snd[i].asset = a;
        }
    }

    xTaskCreatePinnedToCore(loader_task, "sb_loader", 6144, NULL, 1, &s_loader_task, 0);
    return ESP_OK;
}

bool soundbank_available(void) { return s_mounted; }

void soundbank_set_mode(uint8_t mode_index) {
    if (!s_mounted || !s_loader_task) return;
    if (mode_index >= 9) return; // Mode 10 (USB MP3 Player) khong dung soundbank LittleFS
    s_target_mode = mode_index % 9;
    xTaskNotifyGive(s_loader_task);
}

// ---------------------------------------------------------------------------
// Playback helpers
// ---------------------------------------------------------------------------
static bool play_id(snd_id_t id, float gain, float semitone, bool loop, choke_group_t choke,
                    uint8_t owner, uint32_t delay_ms, uint32_t max_ms) {
    if (!s_mounted || id >= SND_COUNT) return false;
    pcm_asset_t a = {0};
    if (xSemaphoreTake(s_bank_mutex, pdMS_TO_TICKS(25)) != pdTRUE) {
        return false;
    }
    a = s_snd[id].asset;
    xSemaphoreGive(s_bank_mutex);

    if (!a.pcm) return false;
    return audio_sample_play(&a, gain, semitone, loop, (uint8_t)choke, owner, delay_ms, max_ms) >= 0;
}

static bool is_loaded(snd_id_t id) { return s_snd[id].asset.pcm != NULL; }

bool sb_play_piano_dur(float freq_hz, uint8_t timbre, uint8_t owner_key, uint32_t max_ms) {
    if (!s_mounted || freq_hz < 20.0f) return false;
    static const int piano_midi[15] = {
        36, 42, 48, 54,
        60, 62, 64, 65, 67, 69, 71,
        72, 78, 84, 96
    };
    static const int bell_midi[4]   = {60, 72, 84, 96};
    float midi = 69.0f + 12.0f * log2f(freq_hz / 440.0f);

    snd_id_t first;
    const int *roots;
    int count;
    float max_shift, gain;
    if (timbre == TIMBRE_PIANO) {
        first = SND_PIANO_C2; roots = piano_midi; count = 15; max_shift = 15.0f; gain = 1.15f;
    } else if (timbre == TIMBRE_MUSIC_BOX) {
        first = SND_BELL_C4; roots = bell_midi; count = 4; max_shift = 15.0f; gain = 1.15f;
    } else {
        return false;   // 8-bit chiptune timbre stays synthesised on purpose
    }

    int best = -1;
    float best_dist = 1000.0f;
    for (int i = 0; i < count; i++) {
        if (!is_loaded((snd_id_t)(first + i))) continue;
        float d = fabsf(midi - (float)roots[i]);
        if (d < best_dist) { best_dist = d; best = i; }
    }
    if (best < 0 || best_dist > max_shift) return false;
    return play_id((snd_id_t)(first + best), gain, midi - (float)roots[best], false, CHOKE_NONE, owner_key, 0, max_ms);
}

bool sb_play_piano(float freq_hz, uint8_t timbre, uint8_t owner_key) {
    return sb_play_piano_dur(freq_hz, timbre, owner_key, 0);
}

bool sb_play_mode_name(uint8_t mode_index) {
    if (!s_mounted || mode_index >= 9) return false;
    snd_id_t id = (snd_id_t)(SND_MODE_1 + mode_index);
    if (!is_loaded(id)) return false;
    return play_id(id, 1.40f, 0, false, CHOKE_SPEECH, 0, 0, 0);
}

bool sb_play_chime(uint8_t mode_index) {
    float f = 440.0f + (mode_index * 55.0f);
    float semi = 12.0f * log2f(f / 440.0f);
    bool ok = play_id(SND_CHIME, 0.9f, semi, false, CHOKE_NONE, 0, 0, 0);
    if (ok) play_id(SND_CHIME, 0.6f, semi + 7.02f, false, CHOKE_NONE, 0, 70, 0);   // perfect fifth
    return ok;
}

bool sb_play_tick(float volume) {
    return play_id(SND_CHIME, 0.55f, volume * 14.0f - 7.0f, false, CHOKE_TICK, 0, 0, 90);
}

bool sb_play_drum_pitched(int drum_type, float semitone) {
    switch (drum_type) {
        case DRUM_KICK:   return play_id(SND_KICK,   1.25f, semitone, false, CHOKE_NONE,   0, 0, 0);
        case DRUM_SNARE:  return play_id(SND_SNARE,  1.20f, semitone, false, CHOKE_NONE,   0, 0, 0);
        case DRUM_HIHAT:  return play_id(SND_HIHAT,  1.10f, semitone, false, CHOKE_HIHAT,  0, 0, 0);
        case DRUM_CYMBAL: return play_id(SND_CYMBAL, 1.10f, semitone, false, CHOKE_CYMBAL, 0, 0, 0);
        case DRUM_TOM:    return play_id(SND_TOM,    1.25f, semitone, false, CHOKE_NONE,   0, 0, 0);
        case DRUM_CLAP:   return play_id(SND_CLAP,   1.20f, semitone, false, CHOKE_NONE,   0, 0, 0);
    }
    return false;
}

bool sb_play_drum(int drum_type) {
    return sb_play_drum_pitched(drum_type, 0.0f);
}

bool sb_play_typewriter(int type) {
    switch (type) {
        case TYPEWRITER_KEY_CLICK:  return play_id(SND_TW_CLICK, 1.25f, 0, false, CHOKE_NONE, 0, 0, 0);
        case TYPEWRITER_SPACE_SLIDE:return play_id(SND_TW_SPACE, 1.20f, 0, false, CHOKE_NONE, 0, 0, 0);
        case TYPEWRITER_ENTER_BELL: return play_id(SND_TW_BELL,  1.25f, 0, false, CHOKE_NONE, 0, 0, 0);
        case TYPEWRITER_BACKSPACE:  return play_id(SND_TW_BACK,  1.20f, 0, false, CHOKE_NONE, 0, 0, 0);
    }
    return false;
}

bool sb_play_cartoon_ex(int sfx, float semitone, uint32_t delay_ms) {
    switch (sfx) {
        case SFX_MARIO_COIN:          return play_id(SND_CT_COIN_M6,      1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0) || play_id(SND_COIN, 1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_SPRING_BOING:        return play_id(SND_CT_BOING,        1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_LASER:               return play_id(SND_CT_LASER,        1.20f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CAR_HORN:            return play_id(SND_CT_HORN,         1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_VICTORY_FANFARE:     return play_id(SND_CT_FANFARE,      1.30f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_WHISTLE_SLIDE:       return play_id(SND_CT_WHISTLE,      1.20f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_WATER_DROP:          return play_id(SND_CT_DROP,         1.30f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_DOORBELL:            return play_id(SND_CT_DOORBELL,     1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_FIRE_SIREN:          return play_id(SND_CT_SIREN,        1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_SKID_STOP:           return play_id(SND_CT_SKID,         1.20f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_JUMP:           return play_id(SND_CT_JUMP,         1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_POWERUP:        return play_id(SND_CT_POWERUP,      1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_1UP:            return play_id(SND_CT_1UP,          1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_FIREBALL:       return play_id(SND_CT_FIREBALL,     1.20f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_WARP:           return play_id(SND_CT_WARP,         1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_BOMB:           return play_id(SND_CT_BOMB,         1.35f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_PACMAN:         return play_id(SND_CT_PACMAN,       1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_SONIC_DASH:     return play_id(SND_CT_SONIC,        1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_PUNCH:          return play_id(SND_CT_PUNCH,        1.30f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_GAME_OVER_TROMBONE:  return play_id(SND_CT_SAD_TROMBONE, 1.30f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_BONGO_RUN:        return play_id(SND_CT_BONGO_RUN,    1.30f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_SLIP_FALL:        return play_id(SND_CT_SLIP_FALL,    1.30f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_ANVIL_CLANG:      return play_id(SND_CT_ANVIL,        1.30f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_SLAPSTICK:        return play_id(SND_CT_SLAPSTICK,    1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_FLEXATONE:        return play_id(SND_CT_FLEXATONE,    1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_VIBRASLAP:        return play_id(SND_CT_VIBRASLAP,    1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_SQUEAK_DUCK:      return play_id(SND_CT_SQUEAK_DUCK,  1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_SPLAT:            return play_id(SND_CT_SPLAT,        1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_BALL_WHISTLE:     return play_id(SND_CT_BALL_WHISTLE, 1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_COWBELL:          return play_id(SND_CT_COWBELL,      1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_MAGIC_WAND:       return play_id(SND_CT_MAGIC_WAND,   1.30f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_GHOST_BOO:        return play_id(SND_CT_GHOST,        1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_TIMPANI_BOOM:     return play_id(SND_CT_TIMPANI,      1.30f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
        case SFX_CT_RUN_AWAY:         return play_id(SND_CT_RUN_AWAY,     1.25f, semitone, false, CHOKE_NONE, 0, delay_ms, 0);
    }
    return false;
}

bool sb_play_cartoon(int sfx) {
    return sb_play_cartoon_ex(sfx, 0.0f, 0);
}

bool sb_play_engine(void) {
    if (is_loaded(SND_COCKPIT_ENGINE)) return play_id(SND_COCKPIT_ENGINE, 1.25f, 0, false, CHOKE_NONE, 0, 0, 0);
    return false;
}
bool sb_play_rocket(void) {
    if (is_loaded(SND_COCKPIT_ROCKET)) return play_id(SND_COCKPIT_ROCKET, 1.25f, 0, false, CHOKE_NONE, 0, 0, 0);
    return false;
}

bool sb_play_cockpit_sfx_delayed(int sfx_id, uint32_t delay_ms) {
    snd_id_t id;
    float gain = 1.25f;
    choke_group_t ck = CHOKE_NONE;
    switch ((cockpit_sfx_t)sfx_id) {
        case COCKPIT_SFX_SIREN:   id = SND_COCKPIT_SIREN;   gain = 1.20f; break;
        case COCKPIT_SFX_HORN:    id = SND_COCKPIT_HORN;    gain = 1.35f; break;
        case COCKPIT_SFX_ENGINE:  id = SND_COCKPIT_ENGINE;  gain = 1.25f; break;
        case COCKPIT_SFX_WATER:   id = SND_COCKPIT_WATER;   gain = 1.30f; break;
        case COCKPIT_SFX_FIRE:    id = SND_COCKPIT_FIRE;    gain = 1.20f; break;
        case COCKPIT_SFX_HELI:    id = SND_COCKPIT_HELI;    gain = 1.25f; break;
        case COCKPIT_SFX_CHEER:   id = SND_COCKPIT_CHEER;   gain = 1.35f; break;
        case COCKPIT_SFX_BELL:    id = SND_COCKPIT_BELL;    gain = 1.30f; break;
        case COCKPIT_SFX_SKID:    id = SND_COCKPIT_SKID;    gain = 1.25f; break;
        case COCKPIT_SFX_ROCKET:  id = SND_COCKPIT_ROCKET;  gain = 1.25f; break;
        case COCKPIT_SFX_REVERSE: id = SND_COCKPIT_REVERSE; gain = 1.25f; break;
        case COCKPIT_SFX_TURN:    id = SND_COCKPIT_TURN;    gain = 1.20f; break;
        case COCKPIT_SFX_RADIO:   id = SND_COCKPIT_RADIO;   gain = 1.40f; ck = CHOKE_SPEECH; break;
        case COCKPIT_SFX_MISSION: id = SND_COCKPIT_MISSION; gain = 1.40f; ck = CHOKE_SPEECH; break;
        case COCKPIT_SFX_SUCCESS: id = SND_COCKPIT_SUCCESS; gain = 1.40f; ck = CHOKE_SPEECH; break;
        default: return false;
    }
    return play_id(id, gain, 0, false, ck, 0, delay_ms, 0);
}

bool sb_play_cockpit_sfx(int sfx_id) {
    return sb_play_cockpit_sfx_delayed(sfx_id, 0);
}

bool sb_play_animal(char key) {
    if (key >= 'a' && key <= 'z') key -= 32;
    if (key < 'A' || key > 'Z') return false;
    snd_id_t id = (snd_id_t)(SND_M8_ANIMAL_A + (key - 'A'));
    return play_id(id, 1.25f, 0, false, CHOKE_ANIMAL, 0, 0, 0);
}

bool sb_play_letter(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 'A' || c > 'Z') return false;
    return play_id((snd_id_t)(SND_LETTER_A + (c - 'A')), 1.40f, 0, false, CHOKE_SPEECH, 0, 0, 0);
}

bool sb_play_number(uint8_t num, uint32_t *duration_ms) {
    if (num > 10) num = 10;
    snd_id_t id = (snd_id_t)(SND_NUM_0 + num);
    if (!is_loaded(id)) return false;
    if (duration_ms && s_snd[id].asset.sample_rate > 0) {
        *duration_ms = (uint32_t)((uint64_t)s_snd[id].asset.num_samples * 1000u / s_snd[id].asset.sample_rate);
    }
    return play_id(id, 1.40f, 0, false, CHOKE_SPEECH, 0, 0, 0);
}

bool sb_play_special_key(uint8_t kc) {
    snd_id_t id;
    switch (kc) {
        case 0x28: case 0x58: id = SND_KEY_ENTER; break;
        case 0x29: id = SND_KEY_ESC; break;
        case 0x2A: id = SND_KEY_BACKSPACE; break;
        case 0x2B: id = SND_KEY_TAB; break;
        case 0x2C: id = SND_KEY_SPACE; break;
        case 0x39: id = SND_KEY_CAPSLOCK; break;
        case 0x43: id = SND_KEY_F10; break;
        case 0x44: id = SND_KEY_F11; break;
        case 0x45: id = SND_KEY_F12; break;
        case 0x46: id = SND_KEY_PRTSCR; break;
        case 0x47: id = SND_KEY_SCROLLLOCK; break;
        case 0x48: id = SND_KEY_PAUSE; break;
        case 0x49: id = SND_KEY_INSERT; break;
        case 0x4A: id = SND_KEY_HOME; break;
        case 0x4B: id = SND_KEY_PAGEUP; break;
        case 0x4C: id = SND_KEY_DELETE; break;
        case 0x4D: id = SND_KEY_END; break;
        case 0x4E: id = SND_KEY_PAGEDOWN; break;
        case 0x4F: id = SND_KEY_ARROW_RIGHT; break;
        case 0x50: id = SND_KEY_ARROW_LEFT; break;
        case 0x51: id = SND_KEY_ARROW_DOWN; break;
        case 0x52: id = SND_KEY_ARROW_UP; break;
        case 0x53: id = SND_KEY_NUMLOCK; break;
        case 0x65: case 0x76: id = SND_KEY_MENU; break;
        case 0xE0: case 0xE4: id = SND_KEY_CTRL; break;
        case 0xE1: case 0xE5: id = SND_KEY_SHIFT; break;
        case 0xE2: case 0xE6: id = SND_KEY_ALT; break;
        case 0xE3: case 0xE7: id = SND_KEY_WIN; break;
        default:
            if (kc >= 0x3A && kc <= 0x42) {
                id = (snd_id_t)(SND_KEY_F1 + (kc - 0x3A));
                break;
            }
            return false;
    }
    return play_id(id, 1.40f, 0, false, CHOKE_SPEECH, 0, 0, 0);
}

bool sb_play_symbol(char c) {
    snd_id_t id;
    switch (c) {
        case '+': id = SND_SYM_PLUS; break;
        case '-': id = SND_SYM_MINUS; break;
        case '*': id = SND_SYM_MULTIPLY; break;
        case '/': id = SND_SYM_DIVIDE; break;
        case '=': id = SND_SYM_EQUAL; break;
        case '.': id = SND_SYM_DOT; break;
        case ',': id = SND_SYM_COMMA; break;
        case ';': id = SND_SYM_SEMICOLON; break;
        case ':': id = SND_SYM_COLON; break;
        case '!': id = SND_SYM_EXCLAIM; break;
        case '?': id = SND_SYM_QUESTION; break;
        case '\'': id = SND_SYM_QUOTE_SINGLE; break;
        case '"': id = SND_SYM_QUOTE_DOUBLE; break;
        case '@': id = SND_SYM_AT; break;
        case '#': id = SND_SYM_HASH; break;
        case '$': id = SND_SYM_DOLLAR; break;
        case '%': id = SND_SYM_PERCENT; break;
        case '^': id = SND_SYM_CARET; break;
        case '&': id = SND_SYM_AMPERSAND; break;
        case '(': id = SND_SYM_PAREN_OPEN; break;
        case ')': id = SND_SYM_PAREN_CLOSE; break;
        case '[': id = SND_SYM_BRACKET_OPEN; break;
        case ']': id = SND_SYM_BRACKET_CLOSE; break;
        case '{': id = SND_SYM_BRACE_OPEN; break;
        case '}': id = SND_SYM_BRACE_CLOSE; break;
        case '<': id = SND_SYM_LESS_THAN; break;
        case '>': id = SND_SYM_GREATER_THAN; break;
        case '\\': id = SND_SYM_BACKSLASH; break;
        case '|': id = SND_SYM_PIPE; break;
        case '~': id = SND_SYM_TILDE; break;
        case '`': id = SND_SYM_BACKTICK; break;
        case '_': id = SND_SYM_UNDERSCORE; break;
        default: return false;
    }
    return play_id(id, 1.40f, 0, false, CHOKE_SPEECH, 0, 0, 0);
}

bool sb_play_praise(void) {
    return play_id(SND_PRAISE, 1.40f, 0, false, CHOKE_SPEECH, 0, 0, 0);
}

bool sb_play_beat(uint32_t delay_ms) {
    return play_id(SND_TOM, 1.15f, 0, false, CHOKE_NONE, 0, delay_ms, 0);
}

bool sb_has_siren(void) {
    return s_mounted && (is_loaded(SND_COCKPIT_SIREN) || is_loaded(SND_CT_SIREN));
}

#define SIREN_OWNER 0xFE
bool sb_siren_loop(bool on) {
    if (!sb_has_siren()) return false;
    snd_id_t sid = is_loaded(SND_COCKPIT_SIREN) ? SND_COCKPIT_SIREN : SND_CT_SIREN;
    if (on) return play_id(sid, 0.72f, 0, true, CHOKE_NONE, SIREN_OWNER, 0, 0);
    audio_sample_release_owner(SIREN_OWNER, 120);
    return true;
}
