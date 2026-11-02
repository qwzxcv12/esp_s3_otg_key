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
    SND_PIANO_C2, SND_PIANO_FS2, SND_PIANO_C3, SND_PIANO_FS3, SND_PIANO_C4,
    SND_PIANO_FS4, SND_PIANO_C5, SND_PIANO_FS5, SND_PIANO_C6,
    SND_BELL_C4, SND_BELL_C5, SND_BELL_C6,
    SND_KICK, SND_SNARE, SND_HIHAT, SND_CYMBAL, SND_TOM, SND_CLAP,
    SND_TW_CLICK, SND_TW_SPACE, SND_TW_BELL, SND_TW_BACK,
    SND_CT_BOING, SND_CT_LASER, SND_CT_HORN, SND_CT_FANFARE, SND_CT_WHISTLE,
    SND_CT_DROP, SND_CT_DOORBELL, SND_CT_SIREN, SND_CT_SKID,
    SND_ENGINE, SND_ROCKET,
    SND_ANIMAL_DOG, SND_ANIMAL_CAT, SND_ANIMAL_BIRD, SND_ANIMAL_COW, SND_ANIMAL_DUCK,
    SND_ANIMAL_FROG, SND_ANIMAL_LION, SND_ANIMAL_PIG, SND_ANIMAL_ELEPHANT, SND_ANIMAL_GOAT,
    SND_ANIMAL_ROOSTER, SND_ANIMAL_OWL, SND_ANIMAL_WOLF, SND_ANIMAL_TIGER, SND_ANIMAL_HORSE,
    SND_ANIMAL_SHEEP,
    SND_LETTER_A,                       // 26 letters follow
    SND_NUM_0 = SND_LETTER_A + 26,      // 11 numbers follow (0..10)
    SND_PRAISE = SND_NUM_0 + 11,
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

    static const char *piano_names[9] = {"c2", "fs2", "c3", "fs3", "c4", "fs4", "c5", "fs5", "c6"};
    for (int i = 0; i < 9; i++) {
        char p[40];
        snprintf(p, sizeof(p), "mode1/piano_%s.wav", piano_names[i]);
        reg((snd_id_t)(SND_PIANO_C2 + i), p, M(0) | M(1));   // Piano + Baby Mozart
    }
    reg(SND_BELL_C4, "mode1/bell_c4.wav", M(0) | M(8));
    reg(SND_BELL_C5, "mode1/bell_c5.wav", M(0) | M(8));
    reg(SND_BELL_C6, "mode1/bell_c6.wav", M(0) | M(8));

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

    reg(SND_CT_BOING,    "mode6/boing.wav",    M(5) | M(8));
    reg(SND_CT_LASER,    "mode6/laser.wav",    M(5) | M(7));
    reg(SND_CT_HORN,     "mode6/horn.wav",     M(5) | M(7));
    reg(SND_CT_FANFARE,  "mode6/fanfare.wav",  M(5) | M(8));
    reg(SND_CT_WHISTLE,  "mode6/whistle.wav",  M(5));
    reg(SND_CT_DROP,     "mode6/drop.wav",     M(5) | M(8));
    reg(SND_CT_DOORBELL, "mode6/doorbell.wav", M(5));
    reg(SND_CT_SIREN,    "mode6/siren.wav",    M(5) | M(7));
    reg(SND_CT_SKID,     "mode6/skid.wav",     M(5) | M(7));

    reg(SND_ENGINE, "mode8/engine.wav", M(7));
    reg(SND_ROCKET, "mode8/rocket.wav", M(7));
    static const char *animals[16] = {"dog", "cat", "bird", "cow", "duck", "frog", "lion", "pig",
                                      "elephant", "goat", "rooster", "owl", "wolf", "tiger", "horse", "sheep"};
    for (int i = 0; i < 16; i++) {
        char p[40];
        snprintf(p, sizeof(p), "mode8/animal_%s.wav", animals[i]);
        reg((snd_id_t)(SND_ANIMAL_DOG + i), p, M(7));
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
    reg(SND_PRAISE, "mode9/praise.wav", M(8));
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

    xTaskCreatePinnedToCore(loader_task, "sb_loader", 6144, NULL, 3, &s_loader_task, 0);
    return ESP_OK;
}

bool soundbank_available(void) { return s_mounted; }

void soundbank_set_mode(uint8_t mode_index) {
    if (!s_mounted || !s_loader_task) return;
    s_target_mode = mode_index % 9;
    xTaskNotifyGive(s_loader_task);
}

// ---------------------------------------------------------------------------
// Playback helpers
// ---------------------------------------------------------------------------
static bool play_id(snd_id_t id, float gain, float semitone, bool loop, choke_group_t choke,
                    uint8_t owner, uint32_t delay_ms, uint32_t max_ms) {
    if (!s_mounted) return false;
    bool ok = false;
    xSemaphoreTake(s_bank_mutex, portMAX_DELAY);
    if (s_snd[id].asset.pcm) {
        ok = audio_sample_play(&s_snd[id].asset, gain, semitone, loop, (uint8_t)choke, owner, delay_ms, max_ms) >= 0;
    }
    xSemaphoreGive(s_bank_mutex);
    return ok;
}

static bool is_loaded(snd_id_t id) { return s_snd[id].asset.pcm != NULL; }

bool sb_play_piano(float freq_hz, uint8_t timbre, uint8_t owner_key) {
    if (!s_mounted || freq_hz < 20.0f) return false;
    static const int piano_midi[9] = {36, 42, 48, 54, 60, 66, 72, 78, 84};
    static const int bell_midi[3]  = {60, 72, 84};
    float midi = 69.0f + 12.0f * log2f(freq_hz / 440.0f);

    snd_id_t first;
    const int *roots;
    int count;
    float max_shift, gain;
    if (timbre == TIMBRE_PIANO) {
        first = SND_PIANO_C2; roots = piano_midi; count = 9; max_shift = 15.0f; gain = 1.15f;
    } else if (timbre == TIMBRE_MUSIC_BOX) {
        first = SND_BELL_C4; roots = bell_midi; count = 3; max_shift = 14.0f; gain = 1.15f;
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
    return play_id((snd_id_t)(first + best), gain, midi - (float)roots[best], false, CHOKE_NONE, owner_key, 0, 0);
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

bool sb_play_drum(int drum_type) {
    switch (drum_type) {
        case DRUM_KICK:   return play_id(SND_KICK,   1.25f, 0, false, CHOKE_NONE,  0, 0, 0);
        case DRUM_SNARE:  return play_id(SND_SNARE,  1.20f, 0, false, CHOKE_NONE,  0, 0, 0);
        case DRUM_HIHAT:  return play_id(SND_HIHAT,  1.10f, 0, false, CHOKE_HIHAT, 0, 0, 0);
        case DRUM_CYMBAL: return play_id(SND_CYMBAL, 1.10f, 0, false, CHOKE_HIHAT, 0, 0, 0);
        case DRUM_TOM:    return play_id(SND_TOM,    1.25f, 0, false, CHOKE_NONE,  0, 0, 0);
        case DRUM_CLAP:   return play_id(SND_CLAP,   1.20f, 0, false, CHOKE_NONE,  0, 0, 0);
    }
    return false;
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

bool sb_play_cartoon(int sfx) {
    switch (sfx) {
        case SFX_MARIO_COIN:       return play_id(SND_COIN,        1.20f, 0, false, CHOKE_NONE, 0, 0, 0);
        case SFX_SPRING_BOING:     return play_id(SND_CT_BOING,    1.20f, 0, false, CHOKE_NONE, 0, 0, 0);
        case SFX_LASER:            return play_id(SND_CT_LASER,    1.20f, 0, false, CHOKE_NONE, 0, 0, 0);
        case SFX_CAR_HORN:         return play_id(SND_CT_HORN,     1.20f, 0, false, CHOKE_NONE, 0, 0, 0);
        case SFX_VICTORY_FANFARE:  return play_id(SND_CT_FANFARE,  1.25f, 0, false, CHOKE_NONE, 0, 0, 0);
        case SFX_WHISTLE_SLIDE:    return play_id(SND_CT_WHISTLE,  1.20f, 0, false, CHOKE_NONE, 0, 0, 0);
        case SFX_WATER_DROP:       return play_id(SND_CT_DROP,     1.25f, 0, false, CHOKE_NONE, 0, 0, 0);
        case SFX_DOORBELL:         return play_id(SND_CT_DOORBELL, 1.25f, 0, false, CHOKE_NONE, 0, 0, 0);
        case SFX_SKID_STOP:        return play_id(SND_CT_SKID,     1.20f, 0, false, CHOKE_NONE, 0, 0, 0);
    }
    return false;   // SFX_FIRE_SIREN is a toggle: see sb_siren_loop()
}

bool sb_play_engine(void) { return play_id(SND_ENGINE, 1.25f, 0, false, CHOKE_NONE, 0, 0, 0); }
bool sb_play_rocket(void) { return play_id(SND_ROCKET, 1.25f, 0, false, CHOKE_NONE, 0, 0, 0); }

bool sb_play_animal(char key) {
    if (key >= 'a' && key <= 'z') key -= 32;
    snd_id_t id;
    switch (key) {
        case 'D': id = SND_ANIMAL_DOG; break;
        case 'C': id = SND_ANIMAL_CAT; break;
        case 'B': id = SND_ANIMAL_BIRD; break;
        case 'M': id = SND_ANIMAL_COW; break;
        case 'Q': id = SND_ANIMAL_DUCK; break;
        case 'F': id = SND_ANIMAL_FROG; break;
        case 'L': id = SND_ANIMAL_LION; break;
        case 'P': id = SND_ANIMAL_PIG; break;
        case 'E': id = SND_ANIMAL_ELEPHANT; break;
        case 'G': id = SND_ANIMAL_GOAT; break;
        case 'R': id = SND_ANIMAL_ROOSTER; break;
        case 'O': id = SND_ANIMAL_OWL; break;
        case 'W': id = SND_ANIMAL_WOLF; break;
        case 'T': id = SND_ANIMAL_TIGER; break;
        case 'H': id = SND_ANIMAL_HORSE; break;
        case 'S': id = SND_ANIMAL_SHEEP; break;
        default: return false;
    }
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
    if (duration_ms) {
        *duration_ms = (uint32_t)((uint64_t)s_snd[id].asset.num_samples * 1000u / s_snd[id].asset.sample_rate);
    }
    return play_id(id, 1.40f, 0, false, CHOKE_SPEECH, 0, 0, 0);
}

bool sb_play_praise(void) {
    return play_id(SND_PRAISE, 1.40f, 0, false, CHOKE_SPEECH, 0, 0, 0);
}

bool sb_play_beat(uint32_t delay_ms) {
    return play_id(SND_TOM, 1.15f, 0, false, CHOKE_NONE, 0, delay_ms, 0);
}

bool sb_has_siren(void) { return s_mounted && is_loaded(SND_CT_SIREN); }

#define SIREN_OWNER 0xFE
bool sb_siren_loop(bool on) {
    if (!sb_has_siren()) return false;
    if (on) return play_id(SND_CT_SIREN, 0.6f, 0, true, CHOKE_NONE, SIREN_OWNER, 0, 0);
    audio_sample_release_owner(SIREN_OWNER, 120);
    return true;
}
