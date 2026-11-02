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
    SFX_SKID_STOP
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
void audio_piano_key_release(uint8_t owner_key);    // key up -> natural 220 ms release
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
void audio_play_mozart_next_note(void);
void audio_mozart_next_song(void);

// Mode 3: Speed Runner
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
void audio_cockpit_rocket_launch(void);
void audio_play_animal_sound(char key);

// Mode 9: Phonics & Counting Learning
void audio_play_phonics_letter(char c);
void audio_play_counting_number(uint8_t num);

#endif // AUDIO_SYNTH_H
