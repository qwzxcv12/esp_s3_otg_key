#ifndef AUDIO_SYNTH_H
#define AUDIO_SYNTH_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "soundbank.h"

// Hardware pinout for MAX98357A on ESP32-S3
#define PIN_I2S_LCK    11  // Word Select / LRCK / WS (LRC)
#define PIN_I2S_DIN    9   // Serial Data / DOUT / DIN
#define PIN_I2S_BCK    10  // Bit Clock / BCLK

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_DEFAULT_VOLUME 0.90f

typedef enum {
    TIMBRE_PIANO = 0,
    TIMBRE_8BIT_MARIO,
    TIMBRE_MUSIC_BOX
} piano_timbre_t;

typedef enum {
    DRUM_KICK = 0,
    DRUM_SNARE,
    DRUM_HIHAT,
    DRUM_CYMBAL,
    DRUM_TOM,
    DRUM_CLAP
} drum_type_t;

typedef enum {
    SFX_MARIO_COIN = 0,
    SFX_SPRING_BOING,
    SFX_LASER,
    SFX_CAR_HORN,
    SFX_VICTORY_FANFARE,
    SFX_WHISTLE_SLIDE,
    SFX_WATER_DROP,
    SFX_DOORBELL,
    SFX_FIRE_SIREN,
    SFX_SKID_STOP,
    // Expanded Arcade & Retro Game SFX
    SFX_GAME_JUMP,
    SFX_GAME_POWERUP,
    SFX_GAME_1UP,
    SFX_GAME_FIREBALL,
    SFX_GAME_WARP,
    SFX_GAME_BOMB,
    SFX_GAME_PACMAN,
    SFX_GAME_SONIC_DASH,
    SFX_GAME_PUNCH,
    SFX_GAME_OVER_TROMBONE,
    // Expanded Cartoon Slapstick & Comedy SFX
    SFX_CT_BONGO_RUN,
    SFX_CT_SLIP_FALL,
    SFX_CT_ANVIL_CLANG,
    SFX_CT_SLAPSTICK,
    SFX_CT_FLEXATONE,
    SFX_CT_VIBRASLAP,
    SFX_CT_SQUEAK_DUCK,
    SFX_CT_SPLAT,
    SFX_CT_BALL_WHISTLE,
    SFX_CT_COWBELL,
    SFX_CT_MAGIC_WAND,
    SFX_CT_GHOST_BOO,
    SFX_CT_TIMPANI_BOOM,
    SFX_CT_RUN_AWAY,
    SFX_CARTOON_COUNT
} cartoon_sfx_t;

typedef enum {
    TYPEWRITER_KEY_CLICK = 0,
    TYPEWRITER_SPACE_SLIDE,
    TYPEWRITER_ENTER_BELL,
    TYPEWRITER_BACKSPACE
} typewriter_sound_t;

// Init & Controls
esp_err_t audio_synth_init(void);

// ---- Sample playback primitives (used by soundbank.c) ----
// Plays a PSRAM PCM asset: semitone shift via linear-interpolated resampling, optional loop,
// choke group (new sound fades the previous one of the same group), owner key (for release) and
// non-blocking start delay. max_ms > 0 auto-fades the voice after that time. Returns voice index or -1.
int  audio_sample_play(const pcm_asset_t *asset, float gain, float semitone, bool loop,
                       uint8_t choke, uint8_t owner, uint32_t delay_ms, uint32_t max_ms);
void audio_sample_release_owner(uint8_t owner, uint32_t release_ms);
void audio_sample_invalidate(const int16_t *pcm);   // stop every voice that reads this buffer
void audio_play_piano_note_key(float freq_hz, piano_timbre_t timbre, uint8_t owner_key);
void audio_piano_key_release(uint8_t owner_key);    // key up -> fast natural 70 ms damper felt release
void audio_play_praise(void);
void audio_synth_set_volume(float vol);
float audio_synth_get_volume(void);
void audio_synth_volume_up(void);
void audio_synth_volume_down(void);
void audio_play_volume_tick(float volume, bool is_up);
void audio_synth_stop_all_background(void);

// Mode 1: Piano & Pentatonic
void audio_play_piano_note(float freq_hz, piano_timbre_t timbre);
void audio_play_chime_mode(uint8_t mode_index);

// Mode 2: Baby Mozart
typedef struct {
    const char *name;
    const char *artist;
    uint8_t song_idx;
    uint8_t total_songs;
    uint16_t note_idx;
    uint16_t total_notes;
    float freq;
    float bass_freq;
    uint8_t timbre;
    bool is_finished;
} mozart_play_info_t;

bool audio_mozart_play_next_note(mozart_play_info_t *out_info);
void audio_play_mozart_next_note(void);
void audio_mozart_next_song(void);
void audio_mozart_prev_song(void);
void audio_mozart_select_song(uint8_t song_index);
void audio_mozart_restart_song(void);
uint8_t audio_mozart_toggle_timbre(void);
uint8_t audio_mozart_get_timbre(void);
const char *audio_mozart_get_timbre_name(void);
void audio_mozart_toggle_autoplay(void);
void audio_mozart_set_autoplay(bool enable);
bool audio_mozart_is_autoplay(void);
void audio_mozart_autoplay_tick(void);
void audio_mozart_get_info(mozart_play_info_t *out_info);

// Mode 3: Speed Runner (Sonic Speed Track & Audio Engine)
typedef struct {
    float freq;
    float bass_freq;
    uint16_t dur_ms;
} speed_note_t;

typedef struct {
    const char *name;
    const char *theme;
    uint8_t track_idx;
    uint8_t total_tracks;
    uint16_t note_idx;
    uint16_t total_notes;
    float freq;
    float current_speed_kmh;
    uint8_t speed_level; // 0=Stop, 1=Walk, 2=Sport, 3=F1, 4=Nitro
    uint16_t combo;
    uint32_t total_notes_played;
    bool is_finished;
} speed_play_info_t;

void audio_speed_engine_init(void);
void audio_speed_engine_reset(void);
void audio_speed_tap_pedal(void);
bool audio_speed_engine_tick(uint32_t delta_ms, speed_play_info_t *out_info, bool *out_note_played);
bool audio_speed_step_note(speed_play_info_t *out_info);
void audio_speed_next_track(void);
void audio_speed_prev_track(void);
void audio_speed_select_track(uint8_t track_idx);
void audio_speed_restart_track(void);
uint8_t audio_speed_toggle_timbre(void);
const char *audio_speed_get_timbre_name(void);
void audio_speed_nitro_boost(void);
void audio_speed_brake(void);
void audio_speed_honk(void);
void audio_speed_get_info(speed_play_info_t *out_info);
float audio_speed_get_current_kmh(void);
uint8_t audio_speed_get_level(void);
void audio_speed_set_auto_pedal(bool enable);
bool audio_speed_is_auto_pedal(void);

// Backward-compatible stubs
void audio_speed_runner_keystroke(void);
void audio_speed_runner_idle_check(void);

// Mode 4: Drum Kit
void audio_play_drum(drum_type_t drum);

// Mode 5: Vintage Typewriter
void audio_play_typewriter(typewriter_sound_t type);

// Mode 6: Cartoon SFX
void audio_play_cartoon_sfx(cartoon_sfx_t sfx);

// Mode 7: Morse Code & Robot
void audio_play_morse_char(char c);
void audio_play_robot_chirp(void);

// Mode 8: Cockpit & Animals
void audio_cockpit_engine_start(void);
void audio_cockpit_engine_stop(void);
void audio_cockpit_siren_toggle(void);
bool audio_cockpit_is_siren_on(void);
void audio_cockpit_siren_set(bool on);
void audio_cockpit_rocket_launch(void);
void audio_cockpit_water_hose(void);
void audio_cockpit_horn(void);
void audio_cockpit_skid(void);
void audio_cockpit_bell(void);
void audio_cockpit_reverse(void);
void audio_cockpit_turn(void);
void audio_cockpit_radio(void);
void audio_cockpit_cheer(void);
void audio_cockpit_fire(void);
void audio_cockpit_helicopter(void);
void audio_cockpit_play_sfx(int sfx_id);
void audio_play_animal_sound(char key);

// Mode 9: Phonics, Counting & Full-Keyboard Voice
void audio_play_phonics_letter(char c);
void audio_play_counting_number(uint8_t num);
void audio_play_special_key(uint8_t kc);
void audio_play_symbol(char c);

#endif // AUDIO_SYNTH_H
