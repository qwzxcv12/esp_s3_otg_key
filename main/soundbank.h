#ifndef SOUNDBANK_H
#define SOUNDBANK_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Real-recording sample bank: WAV files (16-bit PCM mono 24 kHz) live on the LittleFS "storage"
// partition and the sounds of the current mode are loaded into PSRAM when the mode changes.
// Every sb_play_* returns false when its sample is not available (not loaded yet / file missing),
// and the caller then falls back to the built-in synthesiser.

typedef struct {
    int16_t *pcm;          // PSRAM buffer
    uint32_t num_samples;
    uint32_t sample_rate;  // from WAV header
} pcm_asset_t;

// Choke groups: a new sound in a group quickly fades out the previous one of the same group.
typedef enum {
    CHOKE_NONE = 0,
    CHOKE_SPEECH = 1,   // spelled letters / spoken numbers
    CHOKE_ANIMAL = 2,
    CHOKE_HIHAT = 3,
    CHOKE_TICK = 4,     // volume knob ticks
    CHOKE_CYMBAL = 5    // crash cymbals (separate from closed hi-hat!)
} choke_group_t;

esp_err_t soundbank_init(void);                  // mount LittleFS, start loader task
void      soundbank_set_mode(uint8_t mode_index); // async: load this mode's bank, free the old one
bool      soundbank_available(void);              // LittleFS mounted

typedef enum {
    COCKPIT_SFX_SIREN = 0,
    COCKPIT_SFX_HORN,
    COCKPIT_SFX_ENGINE,
    COCKPIT_SFX_WATER,
    COCKPIT_SFX_FIRE,
    COCKPIT_SFX_HELI,
    COCKPIT_SFX_CHEER,
    COCKPIT_SFX_BELL,
    COCKPIT_SFX_SKID,
    COCKPIT_SFX_ROCKET,
    COCKPIT_SFX_REVERSE,
    COCKPIT_SFX_TURN,
    COCKPIT_SFX_RADIO,
    COCKPIT_SFX_MISSION,
    COCKPIT_SFX_SUCCESS,
} cockpit_sfx_t;

// Sample playback helpers (all non-blocking)
bool sb_play_mode_name(uint8_t mode_index);                              // voice announcement: "Chế độ 1: Đàn Piano diệu kỳ!"
bool sb_play_piano(float freq_hz, uint8_t timbre, uint8_t owner_key);   // real piano / bells, pitch shifted
bool sb_play_piano_dur(float freq_hz, uint8_t timbre, uint8_t owner_key, uint32_t max_ms);
bool sb_play_chime(uint8_t mode_index);
bool sb_play_tick(float volume);
bool sb_play_drum(int drum_type);
bool sb_play_drum_pitched(int drum_type, float semitone);
bool sb_play_typewriter(int type);
bool sb_play_cartoon(int sfx);
bool sb_play_cartoon_ex(int sfx, float semitone, uint32_t delay_ms);
bool sb_play_engine(void);
bool sb_play_rocket(void);
bool sb_play_cockpit_sfx(int sfx_id);
bool sb_play_cockpit_sfx_delayed(int sfx_id, uint32_t delay_ms);
bool sb_play_animal(char key);
bool sb_play_letter(char c);
bool sb_play_number(uint8_t num, uint32_t *duration_ms); // spoken number, returns its length
bool sb_play_praise(void);
bool sb_play_special_key(uint8_t kc);                   // special / navigation / modifier keys
bool sb_play_symbol(char c);                            // punctuation / math symbols
bool sb_play_beat(uint32_t delay_ms);                   // counting drum hit
bool sb_siren_loop(bool on);                            // looped siren sample on/off
bool sb_has_siren(void);

#endif // SOUNDBANK_H
