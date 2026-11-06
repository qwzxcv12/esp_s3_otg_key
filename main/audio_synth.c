#include "audio_synth.h"
#include "led_effects.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

static const char *TAG = "AUDIO_SYNTH";

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#define MAX_POLY_VOICES 24
#define MIX_PRE_GAIN 0.85f   // Headroom: tong cac voice nhan he so nay truoc khi qua soft-limiter
#define CHUNK_SAMPLES 256

// Pre-computed Sine Wavetable for Ultra-Fast DSP (512 points)
#define SINE_TABLE_SIZE 512
#define SINE_TABLE_MASK (SINE_TABLE_SIZE - 1)
static float s_sine_table[SINE_TABLE_SIZE];

static void init_sine_table(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        s_sine_table[i] = sinf(2.0f * M_PI * (float)i / (float)SINE_TABLE_SIZE);
    }
}

// Wavetable sine with linear interpolation (removes quantization noise / harshness)
static inline float fast_sin(float phase) {
    uint32_t i = (uint32_t)phase;
    float frac = phase - (float)i;
    float a = s_sine_table[i & SINE_TABLE_MASK];
    float b = s_sine_table[(i + 1) & SINE_TABLE_MASK];
    return a + (b - a) * frac;
}

// Soft limiter (Pade tanh approximation): no hard-clip buzz when several voices overlap
static inline float soft_limit(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

typedef enum {
    VOICE_IDLE = 0,
    VOICE_TONE_ADSR,     // Piano, Music box, 8-bit
    VOICE_PITCH_SWEEP,   // Kick, Laser, Boing, Water drop, Whistle
    VOICE_NOISE_BURST,   // Snare, Hi-hat, Cymbal, Typewriter click
    VOICE_MORSE,         // Morse dit/dah sequence
    VOICE_R2D2,          // Robot chirps
    VOICE_ENGINE_RUMBLE, // Cockpit engine
    VOICE_ANIMAL,        // Animal sounds
    VOICE_SAMPLE         // Real recording from the PSRAM soundbank
} voice_mode_t;

typedef struct {
    bool active;
    voice_mode_t mode;
    float phase;
    float phase_inc;
    float start_freq;
    float end_freq;
    float amp;
    uint32_t pos;
    uint32_t length;
    uint32_t delay;      // samples to wait before the voice starts (non-blocking sequencing)
    float lp;            // filter state (noise high-pass)
    uint8_t sub_type;
    // --- sample playback state ---
    const int16_t *pcm;
    uint32_t pcm_len;
    uint32_t sidx;       // integer read position
    float sfrac;         // fractional read position (keeps precision on long recordings)
    float sstep;         // resampling step (rate ratio * pitch ratio)
    float env;           // 0..1 release envelope
    float rel_decay;     // per-sample env decrement while releasing
    bool releasing;
    bool loop;
    uint8_t choke;
    uint8_t owner;
    uint32_t stop_at;    // auto-release after this many output samples (0 = never)
} voice_t;

static i2s_chan_handle_t s_tx_handle = NULL;
static bool s_audio_ready = false;
static volatile bool s_low_power_mode = false;
static float s_master_volume = AUDIO_DEFAULT_VOLUME;
static bool s_is_muted = false;
static float s_pre_mute_volume = AUDIO_DEFAULT_VOLUME;
static SemaphoreHandle_t s_voice_mutex = NULL;
static voice_t s_voices[MAX_POLY_VOICES];

// Cockpit siren state
static bool s_siren_on = false;
static float s_siren_phase = 0.0f;
static float s_siren_gain = 0.0f;
static uint32_t s_siren_step = 0;

// Streaming Audio Buffer State (USB Flash Drive MP3 / WAV)
#define STREAM_RINGBUF_SAMPLES (64 * 1024)
#define STREAM_PREBUFFER_THRESHOLD (8 * 1024) // Ngưỡng mồi đệm 8192 mẫu (~185ms stereo 44.1kHz)
static int16_t *s_stream_ringbuf = NULL;
static size_t s_stream_wr_idx = 0;
static size_t s_stream_rd_idx = 0;
static size_t s_stream_available = 0;
static portMUX_TYPE s_stream_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_stream_active = false;
static volatile bool s_stream_prebuffering = true;
static uint32_t s_stream_sample_rate = 44100;
static uint8_t s_stream_channels = 2;
static float s_stream_resample_ratio = 1.0f;
static float s_stream_resample_frac = 0.0f;
static volatile float s_stream_current_energy = 0.0f;

// ============================================================================
// HI-FI DSP BIQUAD ENGINE (Direct Form II Transposed) & TRANSPARENT SOFT-KNEE GUARD
// ============================================================================
typedef struct {
    float b0, b1, b2, a1, a2;
    float s1, s2;
} dsp_biquad_t;

static inline void biquad_reset(dsp_biquad_t *f) {
    f->s1 = 0.0f;
    f->s2 = 0.0f;
}

static inline float biquad_process(dsp_biquad_t *f, float in) {
    float out = f->b0 * in + f->s1;
    f->s1 = f->b1 * in - f->a1 * out + f->s2;
    f->s2 = f->b2 * in - f->a2 * out;
    return out;
}

// Subsonic High-Pass Filter (Butterworth 2nd-order, Q=0.7071)
static void biquad_calc_hpf(dsp_biquad_t *f, float fc, float fs) {
    float w0 = 2.0f * (float)M_PI * fc / fs;
    float alpha = sinf(w0) / (2.0f * 0.70710678f);
    float cos_w0 = cosf(w0);
    float b0 = (1.0f + cos_w0) * 0.5f;
    float b1 = -(1.0f + cos_w0);
    float b2 = (1.0f + cos_w0) * 0.5f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cos_w0;
    float a2 = 1.0f - alpha;
    float inv_a0 = 1.0f / a0;
    f->b0 = b0 * inv_a0;
    f->b1 = b1 * inv_a0;
    f->b2 = b2 * inv_a0;
    f->a1 = a1 * inv_a0;
    f->a2 = a2 * inv_a0;
    f->s1 = 0.0f;
    f->s2 = 0.0f;
}

// Peaking EQ Filter (Audio EQ Cookbook)
static void biquad_calc_peaking(dsp_biquad_t *f, float f0, float fs, float gain_db, float Q) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / fs;
    float alpha = sinf(w0) / (2.0f * Q);
    float cos_w0 = cosf(w0);
    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cos_w0;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cos_w0;
    float a2 = 1.0f - alpha / A;
    float inv_a0 = 1.0f / a0;
    f->b0 = b0 * inv_a0;
    f->b1 = b1 * inv_a0;
    f->b2 = b2 * inv_a0;
    f->a1 = a1 * inv_a0;
    f->a2 = a2 * inv_a0;
    f->s1 = 0.0f;
    f->s2 = 0.0f;
}

// Bit-Perfect Transparent Guard with Hermite Soft-Knee (Zero THD for |x| <= 0.90)
static inline float audio_guard_soft_knee(float x) {
    if (x >= -0.90f && x <= 0.90f) {
        return x; // 100% BIT-PERFECT LINEAR PASS-THROUGH
    }
    if (x > 0.90f) {
        if (x >= 1.25f) return 1.0f;
        float t = (x - 0.90f) * (1.0f / 0.35f); // 0.0 to 1.0
        float f = 0.15f * (t * t * t) - 0.40f * (t * t) + 0.35f * t;
        return 0.90f + f;
    } else {
        if (x <= -1.25f) return -1.0f;
        float t = (-x - 0.90f) * (1.0f / 0.35f);
        float f = 0.15f * (t * t * t) - 0.40f * (t * t) + 0.35f * t;
        return -(0.90f + f);
    }
}

// Hi-Fi DSP Filter Instances (Subsonic 75Hz HPF, Punch Bass 140Hz, Vocal/Treble Clarity 4.5kHz)
static dsp_biquad_t s_dsp_subsonic_l, s_dsp_subsonic_r;
static dsp_biquad_t s_dsp_bass_l, s_dsp_bass_r;
static dsp_biquad_t s_dsp_treble_l, s_dsp_treble_r;
static bool s_dsp_enabled = true;
static bool s_dsp_mono_downmix = true;

// ============================================================================
// MOZART 20-SONG MASTER PLAYLIST & HARMONIC AUDIO ENGINE
// ============================================================================
#define F_C2   65.41f
#define F_D2   73.42f
#define F_E2   82.41f
#define F_F2   87.31f
#define F_FS2  92.50f
#define F_G2   98.00f
#define F_A2   110.00f
#define F_AS2  116.54f
#define F_B2   123.47f

#define F_C3   130.81f
#define F_CS3  138.59f
#define F_D3   146.83f
#define F_DS3  155.56f
#define F_E3   164.81f
#define F_F3   174.61f
#define F_FS3  185.00f
#define F_G3   196.00f
#define F_GS3  207.65f
#define F_A3   220.00f
#define F_AS3  233.08f
#define F_B3   246.94f

#define F_C4   261.63f
#define F_CS4  277.18f
#define F_D4   293.66f
#define F_DS4  311.13f
#define F_E4   329.63f
#define F_F4   349.23f
#define F_FS4  369.99f
#define F_G4   392.00f
#define F_GS4  415.30f
#define F_A4   440.00f
#define F_AS4  466.16f
#define F_B4   493.88f

#define F_C5   523.25f
#define F_CS5  554.37f
#define F_D5   587.33f
#define F_DS5  622.25f
#define F_E5   659.25f
#define F_F5   698.46f
#define F_FS5  739.99f
#define F_G5   783.99f
#define F_GS5  830.61f
#define F_A5   880.00f
#define F_AS5  932.33f
#define F_B5   987.77f

#define F_C6   1046.50f
#define F_D6   1174.66f
#define F_E6   1318.51f
#define F_F6   1396.91f
#define F_G6   1567.98f

typedef struct {
    float freq;
    float bass_freq;
    uint16_t dur_ms;
} mozart_note_t;

typedef struct {
    const mozart_note_t *notes;
    size_t count;
    const char *name;
    const char *artist;
} mozart_song_t;

// 1. Rondo Alla Turca (Hành khúc Thổ Nhĩ Kỳ) - W. A. Mozart (K. 331)
static const mozart_note_t s_song_rondo_turca[] = {
    // Đoạn A: Chủ đề La thứ (8 ô nhịp đầu tiên)
    {F_B4, 0, 160}, {F_A4, 0, 160}, {F_GS4, 0, 160}, {F_A4, 0, 160}, {F_C5, F_A3, 340},
    {F_D5, 0, 160}, {F_C5, 0, 160}, {F_B4, 0, 160}, {F_C5, 0, 160}, {F_E5, F_A3, 340},
    {F_F5, 0, 160}, {F_E5, 0, 160}, {F_DS5, 0, 160}, {F_E5, 0, 160}, {F_B5, F_E3, 340},
    {F_A5, 0, 160}, {F_GS5, 0, 160}, {F_A5, 0, 160}, {F_B5, 0, 160}, {F_C6, F_A3, 400},
    {F_A5, 0, 200}, {F_C6, 0, 200}, {F_B5, F_E3, 200}, {F_A5, 0, 200}, {F_GS5, 0, 200}, {F_A5, F_A2, 450},
    // Nhắc lại chủ đề A chuẩn xác nguyên bản Mozart
    {F_B4, 0, 160}, {F_A4, 0, 160}, {F_GS4, 0, 160}, {F_A4, 0, 160}, {F_C5, F_A3, 340},
    {F_D5, 0, 160}, {F_C5, 0, 160}, {F_B4, 0, 160}, {F_C5, 0, 160}, {F_E5, F_A3, 340},
    {F_F5, 0, 160}, {F_E5, 0, 160}, {F_DS5, 0, 160}, {F_E5, 0, 160}, {F_B5, F_E3, 340},
    {F_A5, 0, 160}, {F_GS5, 0, 160}, {F_A5, 0, 160}, {F_B5, 0, 160}, {F_C6, F_A3, 400},
    {F_A5, 0, 200}, {F_C6, 0, 200}, {F_B5, F_E3, 200}, {F_A5, 0, 200}, {F_GS5, 0, 200}, {F_A5, F_A2, 600}
};

// 2. Eine kleine Nachtmusik (Dạ khúc đêm hè) - W. A. Mozart (K. 525)
static const mozart_note_t s_song_eine_kleine[] = {
    // Câu 1: Chủ đề chính Son trưởng dứt khoát
    {F_G4, F_G3, 350}, {F_D4, 0, 200}, {F_G4, F_G3, 350}, {F_D4, 0, 200},
    {F_G4, 0, 180}, {F_D4, 0, 180}, {F_G4, 0, 180}, {F_B4, 0, 180}, {F_D5, F_G3, 500},
    // Câu 2: Câu chuyển hợp âm bậc V (Re)
    {F_C5, F_D3, 350}, {F_A4, 0, 200}, {F_C5, F_D3, 350}, {F_A4, 0, 200},
    {F_C5, 0, 180}, {F_A4, 0, 180}, {F_FS4, 0, 180}, {F_A4, 0, 180}, {F_D4, F_D3, 500},
    // Câu 3: Giai điệu mượt mà của vĩ cầm 1
    {F_G4, F_G3, 250}, {F_G4, 0, 200}, {F_B4, 0, 200}, {F_A4, 0, 200}, {F_G4, F_G2, 400},
    {F_G4, F_G3, 250}, {F_G4, 0, 200}, {F_B4, 0, 200}, {F_A4, 0, 200}, {F_G4, F_G2, 400},
    {F_G4, 0, 180}, {F_G4, 0, 180}, {F_B4, 0, 180}, {F_D5, 0, 180},
    {F_C5, 0, 180}, {F_B4, 0, 180}, {F_A4, F_D2, 260}, {F_G4, F_G2, 700}
};

// 3. Ah! vous dirai-je, maman (Twinkle Twinkle) - W. A. Mozart (K. 265)
static const mozart_note_t s_song_twinkle_full[] = {
    {F_C4, F_C3, 300}, {F_C4, 0, 300}, {F_G4, F_E3, 300}, {F_G4, 0, 300},
    {F_A4, F_F3, 300}, {F_A4, 0, 300}, {F_G4, F_C3, 600},
    {F_F4, F_D3, 300}, {F_F4, 0, 300}, {F_E4, F_C3, 300}, {F_E4, 0, 300},
    {F_D4, F_G3, 300}, {F_D4, 0, 300}, {F_C4, F_C3, 600},
    {F_G4, F_G3, 300}, {F_G4, 0, 300}, {F_F4, F_D3, 300}, {F_F4, 0, 300},
    {F_E4, F_C3, 300}, {F_E4, 0, 300}, {F_D4, F_G3, 600},
    {F_G4, F_G3, 300}, {F_G4, 0, 300}, {F_F4, F_D3, 300}, {F_F4, 0, 300},
    {F_E4, F_C3, 300}, {F_E4, 0, 300}, {F_D4, F_G3, 600},
    {F_C4, F_C3, 300}, {F_C4, 0, 300}, {F_G4, F_E3, 300}, {F_G4, 0, 300},
    {F_A4, F_F3, 300}, {F_A4, 0, 300}, {F_G4, F_C3, 600},
    {F_F4, F_D3, 300}, {F_F4, 0, 300}, {F_E4, F_C3, 300}, {F_E4, 0, 300},
    {F_D4, F_G3, 300}, {F_D4, 0, 300}, {F_C4, F_C2, 700}
};

// 4. Ode to Joy (Khải Hoàn Ca) - L. v. Beethoven (Symphony No. 9)
static const mozart_note_t s_song_ode_to_joy[] = {
    {F_E4, F_C3, 320}, {F_E4, 0, 320}, {F_F4, 0, 320}, {F_G4, F_G3, 320},
    {F_G4, 0, 320}, {F_F4, 0, 320}, {F_E4, F_C3, 320}, {F_D4, 0, 320},
    {F_C4, F_A2, 320}, {F_C4, 0, 320}, {F_D4, 0, 320}, {F_E4, F_C3, 320},
    {F_E4, F_G3, 450}, {F_D4, 0, 200}, {F_D4, F_G2, 600},
    {F_E4, F_C3, 320}, {F_E4, 0, 320}, {F_F4, 0, 320}, {F_G4, F_G3, 320},
    {F_G4, 0, 320}, {F_F4, 0, 320}, {F_E4, F_C3, 320}, {F_D4, 0, 320},
    {F_C4, F_A2, 320}, {F_C4, 0, 320}, {F_D4, 0, 320}, {F_E4, F_C3, 320},
    {F_D4, F_G3, 450}, {F_C4, 0, 200}, {F_C4, F_C2, 600},
    // Đoạn giang tấu Beethoven chính xác (Bridge)
    {F_D4, F_G2, 320}, {F_D4, 0, 320}, {F_E4, 0, 320}, {F_C4, F_C3, 320},
    {F_D4, 0, 320}, {F_E4, 0, 200}, {F_F4, 0, 200}, {F_E4, 0, 320}, {F_C4, F_C3, 320},
    {F_D4, 0, 320}, {F_E4, 0, 200}, {F_F4, 0, 200}, {F_E4, 0, 320}, {F_D4, 0, 320},
    {F_C4, F_A2, 320}, {F_D4, 0, 320}, {F_G3, F_G2, 600},
    // Tái hiện kết thúc
    {F_E4, F_C3, 320}, {F_E4, 0, 320}, {F_F4, 0, 320}, {F_G4, F_G3, 320},
    {F_G4, 0, 320}, {F_F4, 0, 320}, {F_E4, F_C3, 320}, {F_D4, 0, 320},
    {F_C4, F_A2, 320}, {F_C4, 0, 320}, {F_D4, 0, 320}, {F_E4, F_C3, 320},
    {F_D4, F_G3, 450}, {F_C4, 0, 200}, {F_C4, F_C2, 700}
};

// 5. Für Elise (Cho Elise) - L. v. Beethoven (WoO 59 - Nguyên bản chủ đề chính)
static const mozart_note_t s_song_fur_elise[] = {
    // Câu 1: Khởi đầu huyền bí
    {F_E5, 0, 220}, {F_DS5, 0, 220}, {F_E5, 0, 220}, {F_DS5, 0, 220}, {F_E5, 0, 220},
    {F_B4, 0, 220}, {F_D5, 0, 220}, {F_C5, 0, 220}, {F_A4, F_A2, 500},
    {F_C4, 0, 220}, {F_E4, 0, 220}, {F_A4, 0, 220}, {F_B4, F_E3, 500},
    {F_E4, 0, 220}, {F_GS4, 0, 220}, {F_B4, 0, 220}, {F_C5, F_A2, 500},
    // Câu 2: Giải kết La thứ
    {F_E4, 0, 220}, {F_E5, 0, 220}, {F_DS5, 0, 220}, {F_E5, 0, 220}, {F_DS5, 0, 220}, {F_E5, 0, 220},
    {F_B4, 0, 220}, {F_D5, 0, 220}, {F_C5, 0, 220}, {F_A4, F_A2, 500},
    {F_C4, 0, 220}, {F_E4, 0, 220}, {F_A4, 0, 220}, {F_B4, F_E3, 500},
    {F_E4, 0, 220}, {F_C5, 0, 220}, {F_B4, 0, 220}, {F_A4, F_A2, 600},
    // Tái hiện toàn vẹn câu chủ đề
    {F_E5, 0, 220}, {F_DS5, 0, 220}, {F_E5, 0, 220}, {F_DS5, 0, 220}, {F_E5, 0, 220},
    {F_B4, 0, 220}, {F_D5, 0, 220}, {F_C5, 0, 220}, {F_A4, F_A2, 500},
    {F_C4, 0, 220}, {F_E4, 0, 220}, {F_A4, 0, 220}, {F_B4, F_E3, 500},
    {F_E4, 0, 220}, {F_GS4, 0, 220}, {F_B4, 0, 220}, {F_C5, F_A2, 500},
    {F_E4, 0, 220}, {F_E5, 0, 220}, {F_DS5, 0, 220}, {F_E5, 0, 220}, {F_DS5, 0, 220}, {F_E5, 0, 220},
    {F_B4, 0, 220}, {F_D5, 0, 220}, {F_C5, 0, 220}, {F_A4, F_A2, 500},
    {F_C4, 0, 220}, {F_E4, 0, 220}, {F_A4, 0, 220}, {F_B4, F_E3, 500},
    {F_E4, 0, 220}, {F_C5, 0, 220}, {F_B4, 0, 220}, {F_A4, F_A2, 700}
};

// 6. Canon in D - Johann Pachelbel (Nguyên bản giai điệu vĩ cầm & bè trầm chuẩn)
static const mozart_note_t s_song_canon_in_d[] = {
    // 8 nốt hợp âm rải chủ đạo
    {F_FS4, F_D3, 400}, {F_E4, F_A2, 400}, {F_D4, F_B2, 400}, {F_CS4, F_FS2, 400},
    {F_B3,  F_G2, 400}, {F_A3, F_D2, 400}, {F_B3, F_G2, 400}, {F_CS4, F_A2, 400},
    // Giai điệu mượt mà kinh điển (Phrase 1)
    {F_D4,  F_D3, 300}, {F_FS4, 0, 300}, {F_A4, F_A2, 300}, {F_G4, 0, 300},
    {F_FS4, F_B2, 300}, {F_D4, 0, 300}, {F_FS4, F_FS2, 300}, {F_E4, F_A2, 300},
    {F_D4,  F_B2, 300}, {F_B3, 0, 300}, {F_D4, F_G2, 300}, {F_A3, 0, 300},
    {F_G3,  F_G2, 300}, {F_B3, 0, 300}, {F_A3, F_D2, 300}, {F_G3, 0, 300},
    // Giai điệu vút cao và giải kết Rê trưởng (Phrase 2)
    {F_FS3, F_D3, 280}, {F_D4, 0, 280}, {F_E4, F_A2, 280}, {F_CS4, 0, 280},
    {F_D4,  F_B2, 280}, {F_FS4, 0, 280}, {F_A4, F_FS2, 280}, {F_A3, 0, 280},
    {F_B3,  F_G2, 280}, {F_G3, 0, 280}, {F_A3, F_D2, 280}, {F_FS3, 0, 280},
    {F_G3,  F_G2, 280}, {F_B3, 0, 280}, {F_A3, 0, 280}, {F_CS4, F_A2, 300}, {F_D4, F_D3, 700}
};

// 7. Brahms' Lullaby (Bài ca ru con) - Johannes Brahms (Op. 49 No. 4)
static const mozart_note_t s_song_brahms_lullaby[] = {
    // Khổ 1: Lời ru nhẹ nhàng
    {F_E4, F_C3, 280}, {F_E4, 0, 280}, {F_G4, F_C3, 600},
    {F_E4, 0, 280}, {F_E4, 0, 280}, {F_G4, F_C3, 600},
    {F_E4, 0, 260}, {F_G4, 0, 260}, {F_C5, F_F3, 400}, {F_B4, 0, 260},
    {F_A4, F_F3, 350}, {F_A4, 0, 350}, {F_G4, F_C3, 600},
    {F_D4, F_G2, 280}, {F_E4, 0, 280}, {F_F4, F_G3, 400}, {F_D4, 0, 280},
    {F_D4, 0, 280}, {F_E4, 0, 280}, {F_F4, F_G3, 400}, {F_D4, 0, 280},
    {F_D4, 0, 260}, {F_F4, 0, 260}, {F_B4, F_G3, 400}, {F_A4, 0, 260},
    {F_G4, F_C3, 350}, {F_B4, 0, 350}, {F_C5, F_C2, 600},
    // Điệp khúc: Guten Abend, gut' Nacht
    {F_C4, F_C3, 260}, {F_C4, 0, 260}, {F_C5, F_F3, 400}, {F_A4, 0, 260}, {F_F4, 0, 260},
    {F_G4, F_C3, 600},
    {F_C4, 0, 260}, {F_F4, 0, 260}, {F_E4, F_C3, 350}, {F_D4, F_G2, 350}, {F_C4, F_C2, 600},
    {F_C4, F_C3, 260}, {F_C4, 0, 260}, {F_C5, F_F3, 400}, {F_A4, 0, 260}, {F_F4, 0, 260},
    {F_G4, F_C3, 600},
    {F_C4, 0, 260}, {F_F4, 0, 260}, {F_E4, F_C3, 350}, {F_D4, F_G2, 350}, {F_C4, F_C2, 750}
};

// 8. Spring (Mùa Xuân) - Antonio Vivaldi (RV 269)
static const mozart_note_t s_song_spring_vivaldi[] = {
    // Câu 1: Mùa xuân rực rỡ
    {F_E4,  F_E3, 300}, {F_GS4, 0, 180}, {F_GS4, 0, 180}, {F_GS4, F_E3, 180},
    {F_FS4, 0, 180}, {F_E4, 0, 180}, {F_B4, F_B2, 500},
    {F_B4,  F_E3, 180}, {F_GS4, 0, 180}, {F_GS4, 0, 180}, {F_GS4, F_E3, 180},
    {F_FS4, 0, 180}, {F_E4, 0, 180}, {F_B4, F_B2, 500},
    {F_GS4, F_E3, 200}, {F_A4, 0, 200}, {F_B4, 0, 300}, {F_A4, 0, 200},
    {F_GS4, F_B2, 300}, {F_FS4, 0, 300}, {F_E4, F_E2, 500},
    // Câu 2: Câu đáp vĩ cầm thánh thót
    {F_B4,  F_E3, 220}, {F_GS4, 0, 220}, {F_A4, 0, 220}, {F_B4, 0, 220},
    {F_A4,  0, 200}, {F_GS4, 0, 200}, {F_FS4, F_B2, 400},
    {F_B4,  F_E3, 220}, {F_GS4, 0, 220}, {F_A4, 0, 220}, {F_B4, 0, 220},
    {F_A4,  0, 200}, {F_GS4, 0, 200}, {F_FS4, 0, 200}, {F_E4, F_E2, 700}
};

// 9. Minuet in G major - J. S. Bach / C. Petzold (BWV Anh. 114)
static const mozart_note_t s_song_bach_minuet[] = {
    // 8 ô nhịp đầu
    {F_D5, F_G3, 350}, {F_G4, 0, 200}, {F_A4, 0, 200}, {F_B4, 0, 200}, {F_C5, 0, 200},
    {F_D5, F_G3, 350}, {F_G4, 0, 300}, {F_G4, F_G2, 400},
    {F_E5, F_C3, 350}, {F_C5, 0, 200}, {F_D5, 0, 200}, {F_E5, 0, 200}, {F_FS5, 0, 200},
    {F_G5, F_G3, 350}, {F_G4, 0, 300}, {F_G4, F_G2, 400},
    {F_C5, F_C3, 350}, {F_D5, 0, 200}, {F_C5, 0, 200}, {F_B4, 0, 200}, {F_A4, 0, 200},
    {F_B4, F_G3, 350}, {F_C5, 0, 200}, {F_B4, 0, 200}, {F_A4, 0, 200}, {F_G4, 0, 200},
    {F_FS4, F_D3, 350}, {F_G4, 0, 200}, {F_A4, 0, 200}, {F_B4, 0, 200}, {F_G4, 0, 200},
    {F_A4, F_D2, 500}, {F_D4, 0, 300},
    // 8 ô nhịp sau tái hiện kết âm Son trưởng
    {F_D5, F_G3, 350}, {F_G4, 0, 200}, {F_A4, 0, 200}, {F_B4, 0, 200}, {F_C5, 0, 200},
    {F_D5, F_G3, 350}, {F_G4, 0, 300}, {F_G4, F_G2, 400},
    {F_E5, F_C3, 350}, {F_C5, 0, 200}, {F_D5, 0, 200}, {F_E5, 0, 200}, {F_FS5, 0, 200},
    {F_G5, F_G3, 350}, {F_G4, 0, 300}, {F_G4, F_G2, 400},
    {F_C5, F_C3, 350}, {F_D5, 0, 200}, {F_C5, 0, 200}, {F_B4, 0, 200}, {F_A4, 0, 200},
    {F_B4, F_G3, 350}, {F_C5, 0, 200}, {F_B4, 0, 200}, {F_A4, 0, 200}, {F_G4, 0, 200},
    {F_A4, F_D3, 350}, {F_B4, 0, 200}, {F_A4, 0, 200}, {F_G4, 0, 200}, {F_FS4, 0, 200},
    {F_G4, F_G2, 700}
};

// 10. Baby Shark - Pinkfong (Baby Shark + Mommy Shark + Daddy Shark)
static const mozart_note_t s_song_baby_shark_rich[] = {
    // 1. Baby Shark
    {F_D4, F_G3, 250}, {F_E4, 0, 250},
    {F_G4, F_G3, 200}, {F_G4, 0, 200}, {F_G4, 0, 200}, {F_G4, 0, 150}, {F_G4, 0, 150}, {F_G4, 0, 200}, {F_G4, F_G2, 350},
    {F_D4, F_G3, 250}, {F_E4, 0, 250},
    {F_G4, F_G3, 200}, {F_G4, 0, 200}, {F_G4, 0, 200}, {F_G4, 0, 150}, {F_G4, 0, 150}, {F_G4, 0, 200}, {F_G4, F_G2, 350},
    {F_D4, F_G3, 250}, {F_E4, 0, 250},
    {F_G4, F_G3, 200}, {F_G4, 0, 200}, {F_G4, 0, 200}, {F_G4, 0, 150}, {F_G4, 0, 150}, {F_G4, 0, 200}, {F_G4, F_G2, 350},
    {F_G4, F_D3, 250}, {F_G4, 0, 250}, {F_FS4, F_D2, 500},
    // 2. Mommy Shark
    {F_D4, F_C3, 250}, {F_E4, 0, 250},
    {F_G4, F_C3, 200}, {F_G4, 0, 200}, {F_G4, 0, 200}, {F_G4, 0, 150}, {F_G4, 0, 150}, {F_G4, 0, 200}, {F_G4, F_C2, 350},
    {F_D4, F_C3, 250}, {F_E4, 0, 250},
    {F_G4, F_C3, 200}, {F_G4, 0, 200}, {F_G4, 0, 200}, {F_G4, 0, 150}, {F_G4, 0, 150}, {F_G4, 0, 200}, {F_G4, F_C2, 350},
    {F_D4, F_C3, 250}, {F_E4, 0, 250},
    {F_G4, F_C3, 200}, {F_G4, 0, 200}, {F_G4, 0, 200}, {F_G4, 0, 150}, {F_G4, 0, 150}, {F_G4, 0, 200}, {F_G4, F_C2, 350},
    {F_G4, F_D3, 250}, {F_G4, 0, 250}, {F_FS4, F_D2, 700}
};

// 11. Happy Birthday to You (Chúc mừng sinh nhật - Chuẩn Fa trưởng)
static const mozart_note_t s_song_happy_bday_full[] = {
    {F_C4, F_F3, 220}, {F_C4, 0, 220}, {F_D4, 0, 400}, {F_C4, 0, 400}, {F_F4, F_F3, 400}, {F_E4, F_C3, 750},
    {F_C4, F_C3, 220}, {F_C4, 0, 220}, {F_D4, 0, 400}, {F_C4, 0, 400}, {F_G4, F_C3, 400}, {F_F4, F_F3, 750},
    {F_C4, F_F3, 220}, {F_C4, 0, 220}, {F_C5, F_A3, 400}, {F_A4, 0, 400}, {F_F4, F_D3, 400}, {F_E4, 0, 400}, {F_D4, F_AS2, 600},
    {F_AS4, F_AS2, 220}, {F_AS4, 0, 220}, {F_A4, F_F3, 400}, {F_F4, 0, 400}, {F_G4, F_C3, 400}, {F_F4, F_F2, 800}
};

// 12. Mary Had a Little Lamb (Chú cừu nhỏ của Mary - Chuẩn từng nốt và âm tiết)
static const mozart_note_t s_song_mary_lamb[] = {
    // Mary had a little lamb, little lamb, little lamb
    {F_E4, F_C3, 300}, {F_D4, 0, 300}, {F_C4, 0, 300}, {F_D4, 0, 300},
    {F_E4, F_C3, 300}, {F_E4, 0, 300}, {F_E4, F_C3, 500},
    {F_D4, F_G3, 300}, {F_D4, 0, 300}, {F_D4, F_G3, 500},
    {F_E4, F_C3, 300}, {F_G4, 0, 300}, {F_G4, F_C3, 500},
    // Mary had a little lamb, its fleece was white as snow
    {F_E4, F_C3, 300}, {F_D4, 0, 300}, {F_C4, 0, 300}, {F_D4, 0, 300},
    {F_E4, F_C3, 300}, {F_E4, 0, 300}, {F_E4, F_C3, 300},
    {F_D4, F_G3, 300}, {F_D4, 0, 300}, {F_E4, 0, 300}, {F_D4, 0, 300}, {F_C4, F_C2, 600}
};

// 13. Old MacDonald Had a Farm (Nông trại vui vẻ - Khổ hát + Điệp khúc tiếng bò kêu)
static const mozart_note_t s_song_old_macdonald[] = {
    // Old MacDonald had a farm, E-I-E-I-O
    {F_G4, F_G3, 300}, {F_G4, 0, 300}, {F_G4, 0, 300}, {F_D4, F_D3, 300},
    {F_E4, F_C3, 300}, {F_E4, 0, 300}, {F_D4, F_G3, 600},
    {F_B4, F_G3, 300}, {F_B4, 0, 300}, {F_A4, F_D3, 300}, {F_A4, 0, 300}, {F_G4, F_G2, 600},
    // And on that farm he had a cow, E-I-E-I-O
    {F_D4, 0, 200}, {F_D4, 0, 200}, {F_G4, F_G3, 300}, {F_G4, 0, 300}, {F_G4, 0, 300}, {F_D4, F_D3, 300},
    {F_E4, F_C3, 300}, {F_E4, 0, 300}, {F_D4, F_G3, 600},
    {F_B4, F_G3, 300}, {F_B4, 0, 300}, {F_A4, F_D3, 300}, {F_A4, 0, 300}, {F_G4, F_G2, 600},
    // Điệp khúc: With a moo moo here and a moo moo there
    {F_D4, 0, 240}, {F_D4, 0, 240}, {F_G4, F_G3, 240}, {F_G4, 0, 240}, {F_G4, 0, 380},
    {F_D4, 0, 240}, {F_D4, 0, 240}, {F_G4, F_G3, 240}, {F_G4, 0, 240}, {F_G4, 0, 380},
    // Here a moo, there a moo, everywhere a moo moo
    {F_G4, 0, 200}, {F_G4, 0, 200}, {F_G4, 0, 200},
    {F_G4, 0, 200}, {F_G4, 0, 200}, {F_G4, 0, 200},
    {F_G4, 0, 180}, {F_G4, 0, 180}, {F_G4, 0, 180}, {F_G4, 0, 180}, {F_G4, 0, 300},
    // Old MacDonald had a farm, E-I-E-I-O
    {F_G4, F_G3, 300}, {F_G4, 0, 300}, {F_G4, 0, 300}, {F_D4, F_D3, 300},
    {F_E4, F_C3, 300}, {F_E4, 0, 300}, {F_D4, F_G3, 600},
    {F_B4, F_G3, 300}, {F_B4, 0, 300}, {F_A4, F_D3, 300}, {F_A4, 0, 300}, {F_G4, F_G2, 700}
};

// 14. Jingle Bells (Chuông ngân Giáng sinh - Khớp 100% từng từ điệp khúc)
static const mozart_note_t s_song_jingle_bells[] = {
    // Jingle bells, jingle bells, jingle all the way
    {F_E4, F_C3, 260}, {F_E4, 0, 260}, {F_E4, F_C3, 500},
    {F_E4, 0, 260}, {F_E4, 0, 260}, {F_E4, F_C3, 500},
    {F_E4, 0, 260}, {F_G4, 0, 260}, {F_C4, 0, 350}, {F_D4, 0, 200}, {F_E4, F_C3, 600},
    // Oh what fun it is to ride in a one-horse open sleigh, hey!
    {F_F4, F_F3, 260}, {F_F4, 0, 260}, {F_F4, 0, 260}, {F_F4, 0, 260},
    {F_F4, F_F3, 260}, {F_E4, 0, 260}, {F_E4, 0, 260}, {F_E4, 0, 260},
    {F_E4, F_C3, 260}, {F_D4, 0, 260}, {F_D4, 0, 260}, {F_E4, 0, 260}, {F_D4, F_G3, 450}, {F_G4, F_G2, 600},
    // Jingle bells, jingle bells, jingle all the way
    {F_E4, F_C3, 260}, {F_E4, 0, 260}, {F_E4, F_C3, 500},
    {F_E4, 0, 260}, {F_E4, 0, 260}, {F_E4, F_C3, 500},
    {F_E4, 0, 260}, {F_G4, 0, 260}, {F_C4, 0, 350}, {F_D4, 0, 200}, {F_E4, F_C3, 600},
    // Oh what fun it is to ride in a one-horse open sleigh!
    {F_F4, F_F3, 260}, {F_F4, 0, 260}, {F_F4, 0, 260}, {F_F4, 0, 260},
    {F_F4, F_F3, 260}, {F_E4, 0, 260}, {F_E4, 0, 260}, {F_E4, 0, 260},
    {F_G4, F_C3, 260}, {F_G4, 0, 260}, {F_F4, 0, 260}, {F_D4, F_G2, 260}, {F_C4, F_C2, 700}
};

// 15. You Are My Sunshine (Ánh dương của mẹ - Khớp từng từ chuẩn xác)
static const mozart_note_t s_song_you_are_my_sunshine[] = {
    // You are my sunshine, my only sunshine
    {F_C4, F_F3, 300}, {F_F4, 0, 300}, {F_G4, 0, 300}, {F_A4, F_F3, 400}, {F_A4, 0, 500},
    {F_A4, 0, 250}, {F_G4, 0, 250}, {F_A4, F_F3, 300}, {F_F4, 0, 300}, {F_F4, 0, 500},
    // You make me happy when skies are gray
    {F_F4, F_F3, 300}, {F_G4, 0, 300}, {F_A4, 0, 300}, {F_AS4, F_AS2, 400}, {F_D5, 0, 300}, {F_D5, 0, 400},
    {F_C5, 0, 250}, {F_AS4, 0, 250}, {F_A4, F_F3, 600},
    // You'll never know, dear, how much I love you
    {F_F4, F_F3, 300}, {F_G4, 0, 300}, {F_A4, 0, 300}, {F_AS4, F_AS2, 350}, {F_D5, 0, 300}, {F_D5, 0, 350},
    {F_C5, F_C3, 350}, {F_AS4, 0, 350}, {F_A4, F_F3, 350}, {F_F4, 0, 500},
    // Please don't take my sunshine away
    {F_F4, F_F3, 300}, {F_G4, 0, 300}, {F_A4, 0, 300}, {F_AS4, F_AS2, 350},
    {F_A4, 0, 350}, {F_G4, F_C3, 350}, {F_F4, F_F2, 700}
};

// 16. Row, Row, Row Your Boat (Chèo thuyền êm trôi - Chuẩn xác)
static const mozart_note_t s_song_row_your_boat[] = {
    {F_C4, F_C3, 350}, {F_C4, 0, 350}, {F_C4, 0, 350}, {F_D4, 0, 200}, {F_E4, F_C3, 500},
    {F_E4, 0, 300}, {F_D4, 0, 200}, {F_E4, 0, 300}, {F_F4, 0, 200}, {F_G4, F_G3, 600},
    {F_C5, F_C4, 200}, {F_C5, 0, 200}, {F_C5, 0, 200},
    {F_G4, F_E3, 200}, {F_G4, 0, 200}, {F_G4, 0, 200},
    {F_E4, F_C3, 200}, {F_E4, 0, 200}, {F_E4, 0, 200},
    {F_C4, F_C2, 200}, {F_C4, 0, 200}, {F_C4, 0, 200},
    {F_G4, F_G3, 300}, {F_F4, 0, 200}, {F_E4, F_C3, 300}, {F_D4, F_G2, 200}, {F_C4, F_C2, 600}
};

// 17. Kìa con bướm vàng (Frère Jacques - Chuẩn xác 4 câu)
static const mozart_note_t s_song_butterfly_vn[] = {
    {F_C4, F_C3, 300}, {F_D4, 0, 300}, {F_E4, F_C3, 300}, {F_C4, 0, 300},
    {F_C4, F_C3, 300}, {F_D4, 0, 300}, {F_E4, F_C3, 300}, {F_C4, 0, 300},
    {F_E4, F_C3, 300}, {F_F4, 0, 300}, {F_G4, F_C3, 600},
    {F_E4, F_C3, 300}, {F_F4, 0, 300}, {F_G4, F_C3, 600},
    {F_G4, F_G3, 160}, {F_A4, 0, 160}, {F_G4, 0, 160}, {F_F4, 0, 160}, {F_E4, F_C3, 300}, {F_C4, 0, 300},
    {F_G4, F_G3, 160}, {F_A4, 0, 160}, {F_G4, 0, 160}, {F_F4, 0, 160}, {F_E4, F_C3, 300}, {F_C4, 0, 300},
    {F_C4, F_C3, 300}, {F_G3, F_G2, 300}, {F_C4, F_C3, 600},
    {F_C4, F_C3, 300}, {F_G3, F_G2, 300}, {F_C4, F_C2, 600}
};

// 18. Chú ếch con - Phan Nhân (Chuẩn 100% ký âm bản nhạc gốc của nhạc sĩ Phan Nhân)
static const mozart_note_t s_song_chu_ech_con[] = {
    // Kìa chú là chú ếch con (Đồ Mi Son Son La Son)
    {F_C4, F_C3, 260}, {F_E4, 0, 260}, {F_G4, F_C3, 260}, {F_G4, 0, 260}, {F_A4, F_F3, 260}, {F_G4, F_C3, 450},
    // Có hai là hai mắt tròn (Mi Son Mi Rê Đồ Rê)
    {F_E4, 0, 260}, {F_G4, 0, 260}, {F_E4, F_C3, 260}, {F_D4, 0, 260}, {F_C4, 0, 260}, {F_D4, F_G3, 450},
    // Chú ngồi học bài một mình (Đồ Mi Son Son La Son)
    {F_C4, F_C3, 260}, {F_E4, 0, 260}, {F_G4, F_C3, 260}, {F_G4, 0, 260}, {F_A4, F_F3, 260}, {F_G4, F_C3, 450},
    // Bên hố bom kề vườn xoan (Mi Son Rê Mi Đồ Đồ)
    {F_E4, 0, 260}, {F_G4, 0, 260}, {F_D4, F_G3, 260}, {F_E4, 0, 260}, {F_C4, 0, 260}, {F_C4, F_C2, 500},
    // Bao cô cá trê non (Son La Đố Son Son)
    {F_G4, F_G3, 260}, {F_A4, 0, 260}, {F_C5, F_C4, 260}, {F_G4, 0, 260}, {F_G4, F_C3, 450},
    // Cùng bao cô cá rô đực (Son La Đố Son Son Mi)
    {F_G4, F_G3, 260}, {F_A4, 0, 260}, {F_C5, F_C4, 260}, {F_G4, 0, 260}, {F_G4, 0, 260}, {F_E4, F_C3, 450},
    // Tung tăng chiếc vây son (Son La Đố Rế Đố)
    {F_G4, F_G3, 260}, {F_A4, 0, 260}, {F_C5, F_C4, 260}, {F_D5, 0, 260}, {F_C5, F_C4, 450},
    // Nhịp theo tiếng ếch vang lừng (Son La Son Mi Rê Đồ)
    {F_G4, F_G3, 260}, {F_A4, 0, 260}, {F_G4, 0, 260}, {F_E4, 0, 260}, {F_D4, F_G2, 260}, {F_C4, F_C2, 700}
};

// 19. Cháu lên ba - Phạm Minh Tuấn (Chuẩn 100% từng từ và nốt nhạc bản gốc Phạm Minh Tuấn)
static const mozart_note_t s_song_chau_len_ba[] = {
    // Cháu lên ba (Son Mi Son)
    {F_G4, F_C3, 280}, {F_E4, 0, 280}, {F_G4, F_C3, 500},
    // Cháu đi mẫu giáo (Son Mi Son La)
    {F_G4, 0, 280}, {F_E4, 0, 280}, {F_G4, 0, 280}, {F_A4, F_F3, 500},
    // Cô thương cháu (Đố La Đố)
    {F_C5, F_C4, 280}, {F_A4, 0, 280}, {F_C5, F_C4, 500},
    // Vì cháu không khóc nhè (Đố La Son Mi Son)
    {F_C5, F_C4, 280}, {F_A4, 0, 280}, {F_G4, 0, 280}, {F_E4, 0, 280}, {F_G4, F_C3, 500},
    // Không khóc nhè (Mi Rê Mi)
    {F_E4, 0, 280}, {F_D4, 0, 280}, {F_E4, F_C3, 500},
    // Để mẹ trồng cây trái (Son Mi Rê Rê Đồ)
    {F_G4, 0, 280}, {F_E4, 0, 280}, {F_D4, 0, 280}, {F_D4, F_G2, 280}, {F_C4, F_C2, 550},
    // Ba vào nhà máy (Son Mi Son La)
    {F_G4, F_C3, 280}, {F_E4, 0, 280}, {F_G4, 0, 280}, {F_A4, F_F3, 500},
    // Ông bà vui cấy cày (Đố La Son Mi Son)
    {F_C5, F_C4, 280}, {F_A4, 0, 280}, {F_G4, 0, 280}, {F_E4, 0, 280}, {F_G4, F_C3, 500},
    // La là la la (Mi Rê Mi Son)
    {F_E4, 0, 280}, {F_D4, 0, 280}, {F_E4, 0, 280}, {F_G4, F_C3, 500},
    // Là la lá la la (Mi Rê Rê Đồ Đồ)
    {F_E4, 0, 280}, {F_D4, 0, 280}, {F_D4, 0, 280}, {F_C4, 0, 280}, {F_C4, F_C2, 700}
};

// 20. Rửa mặt như mèo - Hàn Ngọc Bích (Chuẩn 100% từng từ và nốt nhạc bản gốc Hàn Ngọc Bích)
static const mozart_note_t s_song_rua_mat_nhu_meo[] = {
    // Meo meo meo (Mi Son Mi)
    {F_E4, F_C3, 280}, {F_G4, 0, 280}, {F_E4, F_C3, 500},
    // Rửa mặt như mèo (Đồ Rê Mi Son)
    {F_C4, 0, 280}, {F_D4, 0, 280}, {F_E4, 0, 280}, {F_G4, F_C3, 500},
    // Sâu sâu lắm (Mi Son La)
    {F_E4, 0, 280}, {F_G4, 0, 280}, {F_A4, F_F3, 500},
    // Chẳng được mẹ yêu (La Son Mi Son)
    {F_A4, F_F3, 280}, {F_G4, 0, 280}, {F_E4, 0, 280}, {F_G4, F_C3, 500},
    // Khăn mặt đâu mà ngồi liếm mép (La Son Mi Rê Đồ Rê Mi)
    {F_A4, F_F3, 280}, {F_G4, 0, 280}, {F_E4, 0, 280}, {F_D4, 0, 280},
    {F_C4, 0, 280}, {F_D4, 0, 280}, {F_E4, F_C3, 500},
    // Đau mắt rồi (Son Mi Rê)
    {F_G4, 0, 280}, {F_E4, 0, 280}, {F_D4, F_G2, 500},
    // Lại khóc meo meo (Rê Mi Rê Đồ)
    {F_D4, 0, 280}, {F_E4, 0, 280}, {F_D4, F_G2, 300}, {F_C4, F_C2, 700}
};

// 20-Song Mozart Playlist
static const mozart_song_t s_mozart_playlist[] = {
    {s_song_rondo_turca,        sizeof(s_song_rondo_turca)/sizeof(mozart_note_t),        "Rondo Alla Turca (Hanh khuc Tho Nhi Ky)", "W. A. Mozart"},
    {s_song_eine_kleine,        sizeof(s_song_eine_kleine)/sizeof(mozart_note_t),        "Eine kleine Nachtmusik (Da khuc dem he)", "W. A. Mozart"},
    {s_song_twinkle_full,       sizeof(s_song_twinkle_full)/sizeof(mozart_note_t),       "Twinkle Twinkle Little Star (Ah! vous)",  "W. A. Mozart"},
    {s_song_ode_to_joy,         sizeof(s_song_ode_to_joy)/sizeof(mozart_note_t),         "Ode to Joy (Khai Hoan Ca - Giao huong so 9)", "L. v. Beethoven"},
    {s_song_fur_elise,          sizeof(s_song_fur_elise)/sizeof(mozart_note_t),          "Fur Elise (Cho Elise)",                   "L. v. Beethoven"},
    {s_song_canon_in_d,         sizeof(s_song_canon_in_d)/sizeof(mozart_note_t),         "Canon in D",                              "Johann Pachelbel"},
    {s_song_brahms_lullaby,     sizeof(s_song_brahms_lullaby)/sizeof(mozart_note_t),     "Brahms' Lullaby (Bai ca ru con)",         "Johannes Brahms"},
    {s_song_spring_vivaldi,     sizeof(s_song_spring_vivaldi)/sizeof(mozart_note_t),     "Spring (Mua Xuan - The Four Seasons)",    "Antonio Vivaldi"},
    {s_song_bach_minuet,        sizeof(s_song_bach_minuet)/sizeof(mozart_note_t),        "Minuet in G major",                       "J. S. Bach"},
    {s_song_baby_shark_rich,    sizeof(s_song_baby_shark_rich)/sizeof(mozart_note_t),    "Baby Shark",                              "Pinkfong"},
    {s_song_happy_bday_full,    sizeof(s_song_happy_bday_full)/sizeof(mozart_note_t),    "Happy Birthday to You",                   "Truyen thong"},
    {s_song_mary_lamb,          sizeof(s_song_mary_lamb)/sizeof(mozart_note_t),          "Mary Had a Little Lamb",                  "Dan ca My"},
    {s_song_old_macdonald,      sizeof(s_song_old_macdonald)/sizeof(mozart_note_t),      "Old MacDonald Had a Farm",                "Thieu nhi"},
    {s_song_jingle_bells,       sizeof(s_song_jingle_bells)/sizeof(mozart_note_t),       "Jingle Bells",                            "J. Pierpont"},
    {s_song_you_are_my_sunshine,sizeof(s_song_you_are_my_sunshine)/sizeof(mozart_note_t),"You Are My Sunshine",                     "J. Davis"},
    {s_song_row_your_boat,      sizeof(s_song_row_your_boat)/sizeof(mozart_note_t),      "Row, Row, Row Your Boat",                 "Dan ca Anh"},
    {s_song_butterfly_vn,       sizeof(s_song_butterfly_vn)/sizeof(mozart_note_t),       "Kia Con Buom Vang (Frere Jacques)",       "Dan ca Phap / VN"},
    {s_song_chu_ech_con,        sizeof(s_song_chu_ech_con)/sizeof(mozart_note_t),        "Chu Ech Con",                             "Phan Nhan"},
    {s_song_chau_len_ba,        sizeof(s_song_chau_len_ba)/sizeof(mozart_note_t),        "Chau Len Ba",                             "Pham Minh Tuan"},
    {s_song_rua_mat_nhu_meo,    sizeof(s_song_rua_mat_nhu_meo)/sizeof(mozart_note_t),    "Rua Mat Nhu Meo",                         "Han Ngoc Bich"}
};

static uint8_t s_mozart_song_idx = 0;
static uint16_t s_mozart_note_idx = 0;
static uint8_t s_mozart_timbre = TIMBRE_PIANO;
static bool s_mozart_autoplay = false;
static int64_t s_mozart_last_tick_us = 0;
static uint32_t s_mozart_next_delay_ms = 400;

// Thread-safe voice triggering
static void trigger_voice_at_owned(voice_mode_t mode, float freq, float start_f, float end_f,
                                   float amp, uint32_t dur_ms, uint8_t sub_type, uint32_t delay_ms, uint8_t owner) {
    if (!s_voice_mutex) return;
    if (xSemaphoreTake(s_voice_mutex, pdMS_TO_TICKS(25)) != pdTRUE) return;

    if (owner != 0) {
        for (int i = 0; i < MAX_POLY_VOICES; i++) {
            voice_t *o = &s_voices[i];
            if (o->active && o->owner == owner) {
                o->owner = 0;
                o->releasing = true;
                o->rel_decay = 1.0f / ((float)(AUDIO_SAMPLE_RATE * 10) / 1000.0f);
            }
        }
    }

    voice_t *chosen = NULL;
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        if (!s_voices[i].active) {
            chosen = &s_voices[i];
            break;
        }
    }
    if (!chosen) {
        uint32_t max_pos = 0;
        int steal_idx = 0;
        for (int i = 0; i < MAX_POLY_VOICES; i++) {
            if (s_voices[i].pos > max_pos) {
                max_pos = s_voices[i].pos;
                steal_idx = i;
            }
        }
        chosen = &s_voices[steal_idx];
    }

    chosen->mode = mode;
    chosen->phase = 0.0f;
    chosen->phase_inc = (float)(SINE_TABLE_SIZE * freq) / (float)AUDIO_SAMPLE_RATE;
    chosen->start_freq = start_f;
    chosen->end_freq = end_f;
    chosen->amp = amp;
    chosen->pos = 0;
    chosen->length = (AUDIO_SAMPLE_RATE * dur_ms) / 1000;
    if (chosen->length == 0) chosen->length = 1;
    chosen->sub_type = sub_type;
    chosen->delay = (AUDIO_SAMPLE_RATE * delay_ms) / 1000;
    chosen->lp = 0.0f;
    chosen->pcm = NULL;
    chosen->env = 1.0f;
    chosen->rel_decay = 0.0f;
    chosen->releasing = false;
    chosen->owner = owner;
    chosen->active = true;

    xSemaphoreGive(s_voice_mutex);
}

static inline void trigger_voice_at(voice_mode_t mode, float freq, float start_f, float end_f,
                                    float amp, uint32_t dur_ms, uint8_t sub_type, uint32_t delay_ms) {
    trigger_voice_at_owned(mode, freq, start_f, end_f, amp, dur_ms, sub_type, delay_ms, 0);
}

static inline void trigger_voice_internal(voice_mode_t mode, float freq, float start_f, float end_f,
                                          float amp, uint32_t dur_ms, uint8_t sub_type) {
    trigger_voice_at_owned(mode, freq, start_f, end_f, amp, dur_ms, sub_type, 0, 0);
}

// ---------------------------------------------------------------------------
// Sample (real recording) voices
// ---------------------------------------------------------------------------
int audio_sample_play(const pcm_asset_t *asset, float gain, float semitone, bool loop,
                      uint8_t choke, uint8_t owner, uint32_t delay_ms, uint32_t max_ms) {
    if (!s_voice_mutex || !asset || !asset->pcm || asset->num_samples < 2) return -1;
    if (xSemaphoreTake(s_voice_mutex, pdMS_TO_TICKS(25)) != pdTRUE) return -1;

    // Disown and quickly fade out prior voice of the same owner key to prevent retrigger release race
    if (owner != 0) {
        for (int i = 0; i < MAX_POLY_VOICES; i++) {
            voice_t *o = &s_voices[i];
            if (o->active && o->owner == owner) {
                o->owner = 0;
                o->releasing = true;
                o->rel_decay = 1.0f / ((float)(AUDIO_SAMPLE_RATE * 8) / 1000.0f);
            }
        }
    }

    // Choke group: fade the previous sound of the same group in ~6 ms instead of stacking
    if (choke != 0) {
        for (int i = 0; i < MAX_POLY_VOICES; i++) {
            voice_t *o = &s_voices[i];
            if (o->active && o->mode == VOICE_SAMPLE && o->choke == choke) {
                o->releasing = true;
                o->rel_decay = 1.0f / ((float)(AUDIO_SAMPLE_RATE * 6) / 1000.0f);
            }
        }
    }

    // Pick a free voice, else steal a releasing one, else the oldest
    int chosen = -1;
    uint32_t oldest = 0;
    int steal = 0;
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        if (!s_voices[i].active) { chosen = i; break; }
        if (s_voices[i].mode == VOICE_SAMPLE && s_voices[i].releasing) chosen = i;
        if (s_voices[i].pos >= oldest) { oldest = s_voices[i].pos; steal = i; }
    }
    if (chosen < 0) chosen = steal;

    float pitch = (semitone == 0.0f) ? 1.0f : powf(2.0f, semitone / 12.0f);
    float rate = (float)asset->sample_rate / (float)AUDIO_SAMPLE_RATE;

    voice_t *vc = &s_voices[chosen];
    memset(vc, 0, sizeof(*vc));
    vc->mode = VOICE_SAMPLE;
    vc->pcm = asset->pcm;
    vc->pcm_len = asset->num_samples;
    vc->sstep = rate * pitch;
    vc->amp = gain;
    vc->env = 1.0f;
    vc->loop = loop;
    vc->choke = choke;
    vc->owner = owner;
    vc->length = 0xFFFFFFFFu;     // ends by sample length, not by the synth length counter
    vc->delay = (AUDIO_SAMPLE_RATE * delay_ms) / 1000;
    vc->stop_at = (max_ms > 0) ? (AUDIO_SAMPLE_RATE * max_ms) / 1000 : 0;
    vc->active = true;

    xSemaphoreGive(s_voice_mutex);
    return chosen;
}

void audio_sample_release_owner(uint8_t owner, uint32_t release_ms) {
    if (!s_voice_mutex || owner == 0) return;
    if (release_ms == 0) release_ms = 10;
    float decay = 1.0f / ((float)(AUDIO_SAMPLE_RATE * release_ms) / 1000.0f);
    if (xSemaphoreTake(s_voice_mutex, pdMS_TO_TICKS(5)) != pdTRUE) return;
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        voice_t *v = &s_voices[i];
        if (v->active && v->owner == owner && !v->releasing) {
            v->releasing = true;
            v->rel_decay = decay;
            v->owner = 0; // Disown so it won't be re-released
        }
    }
    xSemaphoreGive(s_voice_mutex);
}

void audio_sample_invalidate(const int16_t *pcm) {
    if (!s_voice_mutex || !pcm) return;
    if (xSemaphoreTake(s_voice_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
        if (s_voices[i].active && s_voices[i].mode == VOICE_SAMPLE && s_voices[i].pcm == pcm) {
            s_voices[i].active = false;
        }
    }
    xSemaphoreGive(s_voice_mutex);
}

// Low-level audio synthesis task running on Core 1
static void audio_synth_task(void *pvParameters) {
    ESP_LOGI(TAG, "Audio synthesis task started on CPU core %d", xPortGetCoreID());

    int32_t *mix_buffer = (int32_t *)heap_caps_malloc(CHUNK_SAMPLES * 2 * sizeof(int32_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!mix_buffer) {
        mix_buffer = (int32_t *)malloc(CHUNK_SAMPLES * 2 * sizeof(int32_t));
    }

    float float_mix[CHUNK_SAMPLES];

    while (1) {
        if (s_low_power_mode) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        memset(float_mix, 0, sizeof(float_mix));

        xSemaphoreTake(s_voice_mutex, portMAX_DELAY);

        for (int v = 0; v < MAX_POLY_VOICES; v++) {
            voice_t *vc = &s_voices[v];
            if (!vc->active) continue;

            int start_i = 0;
            if (vc->delay > 0) {
                uint32_t skip = (vc->delay > CHUNK_SAMPLES) ? CHUNK_SAMPLES : vc->delay;
                vc->delay -= skip;
                start_i = (int)skip;
            }

            for (int i = start_i; i < CHUNK_SAMPLES; i++) {
                if (vc->pos >= vc->length) {
                    vc->active = false;
                    break;
                }

                float sample = 0.0f;
                float progress = (float)vc->pos / (float)vc->length;

                switch (vc->mode) {
                    case VOICE_TONE_ADSR: {
                        float env = 1.0f;
                        if (progress < 0.04f) {
                            env = progress / 0.04f;
                        } else {
                            float decay = 1.0f - progress;
                            env = decay * decay;
                        }
                        if (vc->releasing) {
                            vc->env -= vc->rel_decay;
                            if (vc->env <= 0.0f) {
                                vc->env = 0.0f;
                                vc->active = false;
                                break;
                            }
                            env *= vc->env;
                        }

                        vc->phase += vc->phase_inc;
                        float s = fast_sin(vc->phase);

                        if (vc->sub_type == TIMBRE_PIANO) {
                            float s2 = fast_sin(vc->phase * 2.0f);
                            sample = (s + 0.35f * s2) * env * vc->amp;
                        } else if (vc->sub_type == TIMBRE_8BIT_MARIO) {
                            float sq = (s + fast_sin(vc->phase * 3.0f) * 0.3333f + fast_sin(vc->phase * 5.0f) * 0.2f) * 0.55f;
                            sample = sq * env * vc->amp;
                        } else {
                            float bell_env = (1.0f - progress) * (1.0f - progress) * vc->env;
                            float s3 = fast_sin(vc->phase * 2.75f);
                            sample = (s + 0.25f * s3) * bell_env * vc->amp;
                        }
                        break;
                    }

                    case VOICE_PITCH_SWEEP: {
                        float cur_f = vc->start_freq + (vc->end_freq - vc->start_freq) * progress;
                        vc->phase += (float)(SINE_TABLE_SIZE * cur_f) / (float)AUDIO_SAMPLE_RATE;
                        float env = 1.0f - progress;
                        float s = fast_sin(vc->phase);
                        if (vc->sub_type >= 1) {
                            s = (s + 0.45f * fast_sin(vc->phase * 2.0f) + 0.2f * fast_sin(vc->phase * 3.0f)) * 0.7f;
                        }
                        if (vc->sub_type == 2 && vc->pos < 220) {
                            float click = ((float)(rand() % 2000) - 1000.0f) / 1000.0f;
                            s += click * 0.5f * (1.0f - (float)vc->pos / 220.0f);
                        }
                        sample = s * env * vc->amp;
                        break;
                    }

                    case VOICE_NOISE_BURST: {
                        float env = (1.0f - progress) * (1.0f - progress);
                        float white = ((float)(rand() % 2000) - 1000.0f) / 1000.0f;
                        if (vc->sub_type >= 1) {
                            float hp = (white - vc->lp) * 0.5f;
                            vc->lp = white;
                            white = hp * 1.4f;
                        }
                        if (vc->sub_type == 2) {
                            float ph = fmodf(progress * 3.0f, 1.0f);
                            env *= (ph < 0.3f) ? 1.0f : 0.3f;
                        }
                        sample = white * env * vc->amp;
                        break;
                    }

                    case VOICE_MORSE: {
                        float env = 1.0f;
                        if (progress < 0.05f) env = progress / 0.05f;
                        else if (progress > 0.95f) env = (1.0f - progress) / 0.05f;
                        vc->phase += vc->phase_inc;
                        sample = fast_sin(vc->phase) * env * vc->amp;
                        break;
                    }

                    case VOICE_R2D2: {
                        float f = vc->start_freq + (vc->end_freq - vc->start_freq) * progress;
                        float env = 1.0f;
                        if (progress < 0.1f) env = progress / 0.1f;
                        else if (progress > 0.8f) env = (1.0f - progress) / 0.2f;
                        vc->phase += (float)(SINE_TABLE_SIZE * f) / (float)AUDIO_SAMPLE_RATE;
                        sample = (fast_sin(vc->phase) + 0.25f * fast_sin(vc->phase * 2.0f)) * 0.8f * env * vc->amp;
                        break;
                    }

                    case VOICE_ENGINE_RUMBLE: {
                        float env = 1.0f;
                        if (progress < 0.1f) env = progress / 0.1f;
                        else if (progress > 0.7f) env = (1.0f - progress) / 0.3f;
                        vc->phase += (float)(SINE_TABLE_SIZE * 80.0f) / (float)AUDIO_SAMPLE_RATE;
                        float pulse = fast_sin(vc->phase) * 0.5f + fast_sin(vc->phase * 2.0f) * 0.35f
                                    + fast_sin(vc->phase * 3.0f) * 0.25f;
                        float n = ((float)(rand() % 1000) - 500.0f) / 1000.0f;
                        sample = (pulse + n * 0.2f) * 0.7f * env * vc->amp;
                        break;
                    }

                    case VOICE_ANIMAL: {
                        float f = vc->start_freq + (vc->end_freq - vc->start_freq) * progress;
                        vc->phase += (float)(SINE_TABLE_SIZE * f) / (float)AUDIO_SAMPLE_RATE;
                        float env = fast_sin(progress * (float)(SINE_TABLE_SIZE / 2));
                        float s = fast_sin(vc->phase) + 0.5f * fast_sin(vc->phase * 2.0f) + 0.3f * fast_sin(vc->phase * 3.0f);
                        s *= 0.55f;
                        if (vc->start_freq < 320.0f) {
                            float gr = fast_sin((float)vc->pos * (float)(SINE_TABLE_SIZE * 35.0f) / (float)AUDIO_SAMPLE_RATE);
                            s *= (0.65f + 0.35f * gr);
                        }
                        sample = s * env * vc->amp;
                        break;
                    }

                    case VOICE_SAMPLE: {
                        if (!vc->pcm) {
                            vc->active = false;
                            break;
                        }
                        if (vc->sidx >= vc->pcm_len) {
                            if (vc->loop && vc->pcm_len > 0) {
                                vc->sidx %= vc->pcm_len;
                            } else {
                                vc->active = false;
                                break;
                            }
                        }

                        int16_t s0 = vc->pcm[vc->sidx];
                        int16_t s1 = (vc->sidx + 1 < vc->pcm_len) ? vc->pcm[vc->sidx + 1] : (vc->loop ? vc->pcm[0] : s0);
                        float raw_s = ((float)s0 + (float)(s1 - s0) * vc->sfrac) * (1.0f / 32768.0f);

                        if (vc->releasing) {
                            vc->env -= vc->rel_decay;
                            if (vc->env <= 0.0f) {
                                vc->env = 0.0f;
                                vc->active = false;
                                break;
                            }
                        }

                        sample = raw_s * vc->amp * vc->env;

                        vc->sfrac += vc->sstep;
                        uint32_t adv = (uint32_t)vc->sfrac;
                        vc->sidx += adv;
                        vc->sfrac -= (float)adv;

                        if (vc->stop_at > 0 && vc->pos >= vc->stop_at && !vc->releasing) {
                            vc->releasing = true;
                            vc->rel_decay = 1.0f / ((float)(AUDIO_SAMPLE_RATE * 18) / 1000.0f);
                        }
                        break;
                    }

                    default:
                        break;
                }

                float_mix[i] += sample;
                vc->pos++;
            }
        }

        // Handle cockpit siren (smooth fade in/out, phase wrapped to keep float precision)
        if (!sb_has_siren() && (s_siren_on || s_siren_gain > 0.001f)) {
            s_siren_step += CHUNK_SAMPLES;
            float siren_t = (float)s_siren_step / (float)AUDIO_SAMPLE_RATE;
            float siren_f = ((fmodf(siren_t, 0.7f) < 0.35f) ? 440.0f : 554.0f);
            float inc = (float)(SINE_TABLE_SIZE * siren_f) / (float)AUDIO_SAMPLE_RATE;
            float target = s_siren_on ? 1.0f : 0.0f;
            for (int i = 0; i < CHUNK_SAMPLES; i++) {
                s_siren_gain += (target - s_siren_gain) * 0.004f;
                s_siren_phase += inc;
                if (s_siren_phase >= (float)SINE_TABLE_SIZE) s_siren_phase -= (float)SINE_TABLE_SIZE;
                float sw = fast_sin(s_siren_phase) + 0.3f * fast_sin(s_siren_phase * 3.0f);
                float_mix[i] += sw * 0.35f * s_siren_gain;
            }
            if (s_siren_step > AUDIO_SAMPLE_RATE * 3600u) s_siren_step = 0;
        }

        xSemaphoreGive(s_voice_mutex);

        // Mix Audio Stream from USB Flash Drive (MP3 / WAV) if active
        float stream_l[CHUNK_SAMPLES] = { 0 };
        float stream_r[CHUNK_SAMPLES] = { 0 };
        bool has_stream = false;

        if (s_stream_active && s_stream_ringbuf) {
            portENTER_CRITICAL(&s_stream_mux);
            size_t avail = s_stream_available;
            bool is_prebuf = s_stream_prebuffering;
            portEXIT_CRITICAL(&s_stream_mux);

            // Cơ chế Pre-buffering Watermark:
            // Tạm giữ im lặng cho đến khi bộ đệm tích lũy đủ ít nhất 8192 mẫu (~185ms stereo 44.1kHz)
            // để bảo đảm luồng âm thanh không bao giờ bị đói buffer khi USB đọc file
            if (is_prebuf) {
                if (avail >= STREAM_PREBUFFER_THRESHOLD) {
                    portENTER_CRITICAL(&s_stream_mux);
                    s_stream_prebuffering = false;
                    portEXIT_CRITICAL(&s_stream_mux);
                } else {
                    avail = 0; // Chưa đủ ngưỡng mồi đệm -> giữ im lặng để task đọc USB nạp đủ
                }
            }

            if (avail > 0) {
                has_stream = true;
                float local_energy = 0.0f;
                static float s_resample_l = 0.0f;
                static float s_resample_r = 0.0f;
                static float s_next_l = 0.0f;
                static float s_next_r = 0.0f;

                for (int i = 0; i < CHUNK_SAMPLES; i++) {
                    while (s_stream_resample_frac >= 1.0f) {
                        if (s_stream_channels == 2) {
                            if (avail >= 2) {
                                s_resample_l = s_next_l;
                                s_resample_r = s_next_r;
                                int16_t raw_l = s_stream_ringbuf[s_stream_rd_idx++];
                                if (s_stream_rd_idx >= STREAM_RINGBUF_SAMPLES) s_stream_rd_idx = 0;
                                int16_t raw_r = s_stream_ringbuf[s_stream_rd_idx++];
                                if (s_stream_rd_idx >= STREAM_RINGBUF_SAMPLES) s_stream_rd_idx = 0;
                                s_next_l = (float)raw_l * (1.0f / 32768.0f);
                                s_next_r = (float)raw_r * (1.0f / 32768.0f);
                                avail -= 2;
                                s_stream_resample_frac -= 1.0f;
                            } else {
                                // Hết buffer giữa chừng -> kẹp resample_frac ngay lập tức
                                s_stream_resample_frac = 0.0f;
                                break;
                            }
                        } else {
                            if (avail >= 1) {
                                s_resample_l = s_next_l;
                                s_resample_r = s_next_r;
                                int16_t raw = s_stream_ringbuf[s_stream_rd_idx++];
                                if (s_stream_rd_idx >= STREAM_RINGBUF_SAMPLES) s_stream_rd_idx = 0;
                                float mono = (float)raw * (1.0f / 32768.0f);
                                s_next_l = mono;
                                s_next_r = mono;
                                avail--;
                                s_stream_resample_frac -= 1.0f;
                            } else {
                                s_stream_resample_frac = 0.0f;
                                break;
                            }
                        }
                    }

                    if (avail == 0) {
                        // Tránh nổ pop/rè khi hết dữ liệu đệm: suy hao dần về 0
                        s_resample_l *= 0.9f;
                        s_resample_r *= 0.9f;
                        s_next_l *= 0.9f;
                        s_next_r *= 0.9f;
                        // Khắc phục triệt để lỗi tích lũy resample_frac:
                        // Tuyệt đối không cộng dồn khi buffer rỗng để tránh vứt bỏ sample mới!
                        s_stream_resample_frac = 0.0f;
                    } else {
                        float frac = s_stream_resample_frac;
                        if (frac < 0.0f) frac = 0.0f;
                        if (frac > 1.0f) frac = 1.0f;
                        stream_l[i] = s_resample_l + (s_next_l - s_resample_l) * frac;
                        stream_r[i] = s_resample_r + (s_next_r - s_resample_r) * frac;

                        s_stream_resample_frac += s_stream_resample_ratio;
                    }

                }

                portENTER_CRITICAL(&s_stream_mux);
                s_stream_available = avail;
                portEXIT_CRITICAL(&s_stream_mux);

                // 1. Smart Mono Downmixing (L + R) cho ampli don MAX98357A:
                if (s_dsp_mono_downmix && s_stream_channels == 2) {
                    for (int i = 0; i < CHUNK_SAMPLES; i++) {
                        float mono = (stream_l[i] + stream_r[i]) * 0.5f;
                        stream_l[i] = mono;
                        stream_r[i] = mono;
                    }
                }

                // 2. Hi-Fi DSP Biquad Filters (Subsonic 75Hz HPF + Warm Bass 140Hz + Vocal/Treble Clarity 4.5kHz):
                if (s_dsp_enabled) {
                    for (int i = 0; i < CHUNK_SAMPLES; i++) {
                        float sl = stream_l[i];
                        sl = biquad_process(&s_dsp_subsonic_l, sl);
                        sl = biquad_process(&s_dsp_bass_l, sl);
                        sl = biquad_process(&s_dsp_treble_l, sl);
                        stream_l[i] = sl;

                        float sr = stream_r[i];
                        sr = biquad_process(&s_dsp_subsonic_r, sr);
                        sr = biquad_process(&s_dsp_bass_r, sr);
                        sr = biquad_process(&s_dsp_treble_r, sr);
                        stream_r[i] = sr;
                    }
                }

                // 3. Tinh toan nang luong am thanh cho LED Visualizer sau khi loc:
                local_energy = 0.0f;
                for (int i = 0; i < CHUNK_SAMPLES; i++) {
                    float e = fabsf(stream_l[i]) + fabsf(stream_r[i]);
                    if (e > local_energy) local_energy = e;
                }

                s_stream_current_energy = s_stream_current_energy * 0.85f + local_energy * 0.15f;
            } else {
                // Buffer dang rong: luon giu s_stream_resample_frac = 0.0f
                s_stream_resample_frac = 0.0f;
            }
        }

        // Convert float mix to 32-bit stereo PCM: transparent soft-knee guard + headroom gain
        const float max_int32 = 2147480000.0f * s_master_volume;
        for (int i = 0; i < CHUNK_SAMPLES; i++) {
            float val_l = float_mix[i];
            float val_r = float_mix[i];
            if (has_stream) {
                // Toi uu Headroom len 0.88f (+2.6dB) an toan tuyet doi nho bo loc Subsonic bao ve mang loa
                val_l += stream_l[i] * 0.88f;
                val_r += stream_r[i] * 0.88f;
            }
            float out_l, out_r;
            if (has_stream) {
                // Doi voi nhac MP3/WAV: Su dung Bit-Perfect Transparent Soft-Knee Guard (0% THD cho |x| <= 0.90)
                out_l = audio_guard_soft_knee(val_l);
                out_r = audio_guard_soft_knee(val_r);
            } else {
                // Doi voi dan synth ao polyphonic 24 giong: dung soft_limit Pade tanh
                out_l = soft_limit(val_l * MIX_PRE_GAIN * 1.6f);
                out_r = soft_limit(val_r * MIX_PRE_GAIN * 1.6f);
            }
            mix_buffer[i * 2]     = (int32_t)(out_l * max_int32);
            mix_buffer[i * 2 + 1] = (int32_t)(out_r * max_int32);
        }

        size_t written = 0;
        i2s_channel_write(s_tx_handle, mix_buffer, CHUNK_SAMPLES * 2 * sizeof(int32_t), &written, pdMS_TO_TICKS(50));
    }
}

esp_err_t audio_synth_init(void) {
    ESP_LOGI(TAG, "Initializing I2S MAX98357A on GPIO WS=%d, DIN=%d, BCLK=%d",
             PIN_I2S_LCK, PIN_I2S_DIN, PIN_I2S_BCK);

    init_sine_table();
    s_voice_mutex = xSemaphoreCreateMutex();
    memset(s_voices, 0, sizeof(s_voices));

    float fs = (float)AUDIO_SAMPLE_RATE;
    biquad_calc_hpf(&s_dsp_subsonic_l, 75.0f, fs);
    biquad_calc_hpf(&s_dsp_subsonic_r, 75.0f, fs);
    biquad_calc_peaking(&s_dsp_bass_l, 140.0f, fs, 3.0f, 1.0f);
    biquad_calc_peaking(&s_dsp_bass_r, 140.0f, fs, 3.0f, 1.0f);
    biquad_calc_peaking(&s_dsp_treble_l, 4500.0f, fs, 2.2f, 0.8f);
    biquad_calc_peaking(&s_dsp_treble_r, 4500.0f, fs, 2.2f, 0.8f);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 256;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    slot_cfg.ws_width = 32;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_I2S_BCK,
            .ws   = (gpio_num_t)PIN_I2S_LCK,
            .dout = (gpio_num_t)PIN_I2S_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {0},
        },
    };

    ret = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S std mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    s_audio_ready = true;
    xTaskCreatePinnedToCore(audio_synth_task, "audio_synth", 6144, NULL, 6, NULL, 1);

    ESP_LOGI(TAG, "I2S Audio Engine ready at %d Hz (Real-time Priority 6)!", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

void audio_synth_set_volume(float vol) {
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    s_master_volume = vol;
    ESP_LOGI(TAG, "Master Volume: %d%%", (int)(s_master_volume * 100));
}

float audio_synth_get_volume(void) {
    return s_master_volume;
}

void audio_synth_volume_up(void) {
    if (s_is_muted) s_is_muted = false;
    audio_synth_set_volume(s_master_volume + 0.05f);
}

void audio_synth_volume_down(void) {
    if (s_is_muted) s_is_muted = false;
    audio_synth_set_volume(s_master_volume - 0.05f);
}

bool audio_synth_is_muted(void) {
    return s_is_muted;
}

void audio_play_mute_feedback(bool is_muted) {
    if (is_muted) {
        // Âm thanh báo Tắt tiếng (MUTE / OFF): Arcade Power-Down rõ ràng, đanh gọn (988Hz -> 659Hz)
        trigger_voice_internal(VOICE_TONE_ADSR, 987.77f, 0, 0, 0.90f, 130, TIMBRE_8BIT_MARIO);
        vTaskDelay(pdMS_TO_TICKS(110));
        trigger_voice_internal(VOICE_TONE_ADSR, 659.25f, 0, 0, 0.85f, 200, TIMBRE_8BIT_MARIO);
        vTaskDelay(pdMS_TO_TICKS(220)); // Chờ âm thanh phát xong hoàn toàn qua DMA I2S
    } else {
        // Âm thanh báo Bật lại tiếng (UNMUTE / ON): 3 nốt vút lên vui tươi (E5 659Hz -> B5 988Hz -> E6 1318Hz)
        trigger_voice_internal(VOICE_TONE_ADSR, 659.25f, 0, 0, 0.85f, 100, TIMBRE_8BIT_MARIO);
        vTaskDelay(pdMS_TO_TICKS(85));
        trigger_voice_internal(VOICE_TONE_ADSR, 987.77f, 0, 0, 0.90f, 110, TIMBRE_8BIT_MARIO);
        vTaskDelay(pdMS_TO_TICKS(95));
        trigger_voice_internal(VOICE_TONE_ADSR, 1318.51f, 0, 0, 0.95f, 240, TIMBRE_8BIT_MARIO);
    }
}

bool audio_synth_toggle_mute(void) {
    if (!s_is_muted) {
        // Lưu mức volume hiện tại trước
        s_pre_mute_volume = s_master_volume;
        if (s_pre_mute_volume < 0.20f) s_pre_mute_volume = AUDIO_DEFAULT_VOLUME;
        // Đảm bảo volume đang đủ lớn để phát âm thanh Mute rõ ràng
        if (s_master_volume < 0.40f) s_master_volume = 0.50f;
        // Phát âm thanh báo Mute
        audio_play_mute_feedback(true);
        // Sau khi âm thanh đã ngân xong hoàn toàn -> mới hạ volume về 0
        s_master_volume = 0.0f;
        s_is_muted = true;
    } else {
        // Khôi phục volume trước -> rồi phát âm thanh Unmute vui tươi
        s_master_volume = s_pre_mute_volume;
        s_is_muted = false;
        audio_play_mute_feedback(false);
    }
    return s_is_muted;
}

void audio_play_led_mode_feedback(uint8_t style) {
    if (s_is_muted) return;
    switch (style) {
        case 0: // REACTIVE: Âm thanh trong trẻo nốt Đố (C5 523Hz + G5 784Hz ngân vang)
            trigger_voice_internal(VOICE_TONE_ADSR, 523.25f, 0, 0, 0.85f, 150, TIMBRE_MUSIC_BOX);
            trigger_voice_internal(VOICE_TONE_ADSR, 783.99f, 0, 0, 0.75f, 200, TIMBRE_MUSIC_BOX);
            break;
        case 1: // RAINBOW: Chuỗi arpeggio 3 nốt vút lên vui tươi (C5 523Hz -> E5 659Hz -> G5 784Hz)
            trigger_voice_internal(VOICE_TONE_ADSR, 523.25f, 0, 0, 0.80f, 100, TIMBRE_MUSIC_BOX);
            vTaskDelay(pdMS_TO_TICKS(75));
            trigger_voice_internal(VOICE_TONE_ADSR, 659.25f, 0, 0, 0.85f, 110, TIMBRE_MUSIC_BOX);
            vTaskDelay(pdMS_TO_TICKS(75));
            trigger_voice_internal(VOICE_TONE_ADSR, 783.99f, 0, 0, 0.90f, 220, TIMBRE_MUSIC_BOX);
            break;
        case 2: // BREATHING: Nốt chuông ấm ngân vang êm dịu (A5 880Hz)
            trigger_voice_internal(VOICE_TONE_ADSR, 880.00f, 0, 0, 0.85f, 320, TIMBRE_MUSIC_BOX);
            break;
        case 3: // STARRY: Chuông sao băng lung linh (C6 1046Hz + G6 1568Hz)
            trigger_voice_internal(VOICE_TONE_ADSR, 1046.50f, 0, 0, 0.85f, 120, TIMBRE_MUSIC_BOX);
            vTaskDelay(pdMS_TO_TICKS(80));
            trigger_voice_internal(VOICE_TONE_ADSR, 1567.98f, 0, 0, 0.90f, 240, TIMBRE_MUSIC_BOX);
            break;
        case 4: // OFF: Âm thanh tắt đèn hạ cung (G5 784Hz -> E5 659Hz)
            trigger_voice_internal(VOICE_TONE_ADSR, 783.99f, 0, 0, 0.80f, 90, TIMBRE_8BIT_MARIO);
            vTaskDelay(pdMS_TO_TICKS(85));
            trigger_voice_internal(VOICE_TONE_ADSR, 659.25f, 0, 0, 0.75f, 150, TIMBRE_8BIT_MARIO);
            break;
        default:
            audio_play_volume_tick(s_master_volume, true);
            break;
    }
}

void audio_play_song_change_chime(void) {
    if (s_is_muted) return;
    // Chime báo chuyển bài hát chuẩn: G5 -> C6 vui tươi, bắt tai
    trigger_voice_internal(VOICE_TONE_ADSR, 783.99f, 0, 0, 0.85f, 90, TIMBRE_MUSIC_BOX);
    vTaskDelay(pdMS_TO_TICKS(75));
    trigger_voice_internal(VOICE_TONE_ADSR, 1046.50f, 0, 0, 0.90f, 220, TIMBRE_MUSIC_BOX);
    vTaskDelay(pdMS_TO_TICKS(80));
}

void audio_play_action_feedback(void) {
    if (s_is_muted) return;
    audio_play_praise();
}

void audio_play_piano_timbre_preview(piano_timbre_t timbre) {
    if (s_is_muted) return;
    audio_play_piano_note_dur(523.25f, timbre, 0, 160);
    vTaskDelay(pdMS_TO_TICKS(80));
    audio_play_piano_note_dur(659.25f, timbre, 0, 160);
    vTaskDelay(pdMS_TO_TICKS(80));
    audio_play_piano_note_dur(783.99f, timbre, 0, 320);
}

void audio_play_typewriter_return(void) {
    if (s_is_muted) return;
    audio_play_typewriter(TYPEWRITER_ENTER_BELL);
    vTaskDelay(pdMS_TO_TICKS(100));
    audio_play_typewriter(TYPEWRITER_SPACE_SLIDE);
}

void audio_play_volume_tick(float volume, bool is_up) {
    if (volume <= 0.01f) return;
    if (sb_play_tick(volume)) return;
    float base_freq = 380.0f + (volume * 820.0f);
    trigger_voice_internal(VOICE_TONE_ADSR, base_freq, 0, 0, 0.70f, 50, TIMBRE_MUSIC_BOX);
}

void audio_synth_stop_all_background(void) {
    s_siren_on = false;
    s_siren_gain = 0.0f;
    sb_siren_loop(false);
}

// Mode 1: Piano Note
void audio_play_piano_note(float freq_hz, piano_timbre_t timbre) {
    trigger_voice_internal(VOICE_TONE_ADSR, freq_hz, 0, 0, 0.85f, 750, timbre);
}

void audio_play_piano_note_dur(float freq_hz, piano_timbre_t timbre, uint8_t owner_key, uint32_t max_ms) {
    if (!sb_play_piano_dur(freq_hz, (uint8_t)timbre, owner_key, max_ms)) {
        uint32_t dur = (max_ms > 0) ? max_ms : 750;
        trigger_voice_at_owned(VOICE_TONE_ADSR, freq_hz, 0, 0, 0.85f, dur, timbre, 0, owner_key);
    }
}

void audio_play_piano_note_key(float freq_hz, piano_timbre_t timbre, uint8_t owner_key) {
    audio_play_piano_note_dur(freq_hz, timbre, owner_key, 0);
}

void audio_piano_key_release(uint8_t owner_key) {
    audio_sample_release_owner(owner_key, 320); // Natural damper release for both sample and synth voices
}

void audio_play_praise(void) {
    if (!sb_play_praise()) {
        audio_play_cartoon_sfx(SFX_VICTORY_FANFARE);
    }
}

// Joyful C-major arpeggio + boing when a toddler slaps their whole palm on the keyboard (RollOver)
void audio_play_slap_chord(void) {
    sb_play_cartoon_ex(SFX_SPRING_BOING, 0.0f, 0);
    trigger_voice_at(VOICE_TONE_ADSR, F_C5, 0, 0, 0.65f, 240, TIMBRE_MUSIC_BOX, 0);
    trigger_voice_at(VOICE_TONE_ADSR, F_E5, 0, 0, 0.65f, 240, TIMBRE_MUSIC_BOX, 35);
    trigger_voice_at(VOICE_TONE_ADSR, F_G5, 0, 0, 0.65f, 260, TIMBRE_MUSIC_BOX, 70);
    trigger_voice_at(VOICE_TONE_ADSR, F_C6, 0, 0, 0.70f, 340, TIMBRE_MUSIC_BOX, 105);
}

// Mode change melodic chime
void audio_play_chime_mode(uint8_t mode_index) {
    float root_f = 440.0f + (mode_index * 55.0f);
    trigger_voice_internal(VOICE_TONE_ADSR, root_f, 0, 0, 0.7f, 250, TIMBRE_MUSIC_BOX);
    trigger_voice_internal(VOICE_TONE_ADSR, root_f * 1.5f, 0, 0, 0.8f, 400, TIMBRE_MUSIC_BOX);
}

// ============================================================================
// MODE 2: BABY MOZART AUDIO ENGINE IMPLEMENTATION
// ============================================================================
const char *audio_mozart_get_timbre_name(void) {
    switch (s_mozart_timbre) {
        case TIMBRE_PIANO: return "Dai Duong Cam (Grand Piano)";
        case TIMBRE_MUSIC_BOX: return "Hop Nhac Than Tien (Music Box / Bell)";
        case TIMBRE_8BIT_MARIO: return "Dan Go Marimba (8-Bit Arcade)";
        default: return "Dan Piano Co Dien";
    }
}

uint8_t audio_mozart_toggle_timbre(void) {
    s_mozart_timbre = (s_mozart_timbre + 1) % 3;
    // Play a gentle preview chord in the new timbre
    audio_play_piano_note_dur(F_C5, s_mozart_timbre, 0, 260);
    return s_mozart_timbre;
}

uint8_t audio_mozart_get_timbre(void) {
    return s_mozart_timbre;
}

void audio_mozart_get_info(mozart_play_info_t *out_info) {
    if (!out_info) return;
    const mozart_song_t *curr = &s_mozart_playlist[s_mozart_song_idx];
    out_info->name = curr->name;
    out_info->artist = curr->artist;
    out_info->song_idx = s_mozart_song_idx;
    out_info->total_songs = sizeof(s_mozart_playlist) / sizeof(s_mozart_playlist[0]);
    out_info->note_idx = s_mozart_note_idx + 1;
    out_info->total_notes = curr->count;
    out_info->freq = curr->notes[s_mozart_note_idx].freq;
    out_info->bass_freq = curr->notes[s_mozart_note_idx].bass_freq;
    out_info->timbre = s_mozart_timbre;
    out_info->is_finished = false;
}

void audio_mozart_restart_song(void) {
    s_mozart_note_idx = 0;
    s_mozart_last_tick_us = esp_timer_get_time();
    s_mozart_next_delay_ms = 300;
}

void audio_mozart_next_song(void) {
    s_mozart_song_idx = (s_mozart_song_idx + 1) % (sizeof(s_mozart_playlist) / sizeof(s_mozart_playlist[0]));
    s_mozart_note_idx = 0;
    s_mozart_last_tick_us = esp_timer_get_time();
    s_mozart_next_delay_ms = 400;
    const mozart_song_t *curr = &s_mozart_playlist[s_mozart_song_idx];
    ESP_LOGI(TAG, "Switched Mozart song to: %s (%s)", curr->name, curr->artist);
    audio_play_piano_note_dur(curr->notes[0].freq, s_mozart_timbre, 0, 260);
}

void audio_mozart_prev_song(void) {
    size_t total = sizeof(s_mozart_playlist) / sizeof(s_mozart_playlist[0]);
    s_mozart_song_idx = (s_mozart_song_idx + total - 1) % total;
    s_mozart_note_idx = 0;
    s_mozart_last_tick_us = esp_timer_get_time();
    s_mozart_next_delay_ms = 400;
    const mozart_song_t *curr = &s_mozart_playlist[s_mozart_song_idx];
    ESP_LOGI(TAG, "Switched Mozart song to: %s (%s)", curr->name, curr->artist);
    audio_play_piano_note_dur(curr->notes[0].freq, s_mozart_timbre, 0, 260);
}

void audio_mozart_select_song(uint8_t song_index) {
    size_t total = sizeof(s_mozart_playlist) / sizeof(s_mozart_playlist[0]);
    if (song_index >= total) song_index = total - 1;
    s_mozart_song_idx = song_index;
    s_mozart_note_idx = 0;
    s_mozart_last_tick_us = esp_timer_get_time();
    s_mozart_next_delay_ms = 400;
    const mozart_song_t *curr = &s_mozart_playlist[s_mozart_song_idx];
    ESP_LOGI(TAG, "Selected Mozart song: %s (%s)", curr->name, curr->artist);
    audio_play_piano_note_dur(curr->notes[0].freq, s_mozart_timbre, 0, 260);
}

bool audio_mozart_play_next_note(mozart_play_info_t *out_info) {
    const mozart_song_t *curr = &s_mozart_playlist[s_mozart_song_idx];
    mozart_note_t n = curr->notes[s_mozart_note_idx];

    // 1. Play main melody note
    if (!sb_play_piano(n.freq, s_mozart_timbre, 0)) {
        trigger_voice_internal(VOICE_TONE_ADSR, n.freq, 0, 0, 0.90f, n.dur_ms * 2 + 50, s_mozart_timbre);
    }

    // 2. Play harmonic bass accompaniment simultaneously if present
    if (n.bass_freq > 20.0f) {
        if (!sb_play_piano(n.bass_freq, s_mozart_timbre, 0)) {
            trigger_voice_internal(VOICE_TONE_ADSR, n.bass_freq, 0, 0, 0.70f, n.dur_ms * 2 + 100, s_mozart_timbre);
        }
    }

    bool finished = false;
    uint16_t played_idx = s_mozart_note_idx;
    s_mozart_note_idx++;
    if (s_mozart_note_idx >= curr->count) {
        finished = true;
        s_mozart_note_idx = 0;
        s_mozart_song_idx = (s_mozart_song_idx + 1) % (sizeof(s_mozart_playlist) / sizeof(s_mozart_playlist[0]));
    }

    if (out_info) {
        out_info->name = curr->name;
        out_info->artist = curr->artist;
        out_info->song_idx = s_mozart_song_idx;
        out_info->total_songs = sizeof(s_mozart_playlist) / sizeof(s_mozart_playlist[0]);
        out_info->note_idx = played_idx + 1;
        out_info->total_notes = curr->count;
        out_info->freq = n.freq;
        out_info->bass_freq = n.bass_freq;
        out_info->timbre = s_mozart_timbre;
        out_info->is_finished = finished;
    }
    return finished;
}

void audio_play_mozart_next_note(void) {
    audio_mozart_play_next_note(NULL);
}

void audio_mozart_toggle_autoplay(void) {
    s_mozart_autoplay = !s_mozart_autoplay;
    s_mozart_last_tick_us = esp_timer_get_time();
    s_mozart_next_delay_ms = 400;
}

void audio_mozart_set_autoplay(bool enable) {
    s_mozart_autoplay = enable;
    if (enable) {
        s_mozart_last_tick_us = esp_timer_get_time();
        s_mozart_next_delay_ms = 400;
    }
}

bool audio_mozart_is_autoplay(void) {
    return s_mozart_autoplay;
}

void audio_mozart_autoplay_tick(void) {
    if (!s_mozart_autoplay) return;
    int64_t now = esp_timer_get_time();
    if (now - s_mozart_last_tick_us < (int64_t)s_mozart_next_delay_ms * 1000) {
        return;
    }

    const mozart_song_t *curr = &s_mozart_playlist[s_mozart_song_idx];
    s_mozart_next_delay_ms = curr->notes[s_mozart_note_idx].dur_ms;
    s_mozart_last_tick_us = now;

    mozart_play_info_t info;
    bool finished = audio_mozart_play_next_note(&info);
    led_effects_mozart_note(info.freq, info.bass_freq, info.timbre);

    if (finished) {
        printf("\r\n>>> [LULLABY AUTO-PLAY: CHUYEN TIEP SANG BAI: \"%s\" (%s)] <<<\r\n",
               s_mozart_playlist[s_mozart_song_idx].name,
               s_mozart_playlist[s_mozart_song_idx].artist);
        s_mozart_next_delay_ms = 1500;
    }
}

// ============================================================================
// MODE 3: NHẠC CHẠY THEO TỐC ĐỘ GÕ PHÍM (SPEED MELODY PLAYER)
// ============================================================================

typedef struct {
    const speed_note_t *notes;
    size_t count;
    const char *name;
    const char *title_vi;
} speed_song_def_t;

// 1. Baby Shark (Cá Mập Con)
static const speed_note_t s_song_babyshark[] = {
    {F_D4, F_G2}, {F_E4, 0},
    {F_G4, F_G3}, {F_G4, 0}, {F_G4, F_G3}, {F_G4, 0}, {F_G4, F_G3}, {F_G4, 0}, {F_G4, F_G3},
    {F_D4, F_G2}, {F_E4, 0},
    {F_G4, F_G3}, {F_G4, 0}, {F_G4, F_G3}, {F_G4, 0}, {F_G4, F_G3}, {F_G4, 0}, {F_G4, F_G3},
    {F_D4, F_G2}, {F_E4, 0},
    {F_G4, F_G3}, {F_G4, 0}, {F_G4, F_G3}, {F_G4, 0}, {F_G4, F_G3}, {F_G4, 0}, {F_G4, F_G3},
    {F_G4, F_G3}, {F_G4, 0}, {F_FS4, F_D3}
};

// 2. Kìa Con Bướm Vàng (Frère Jacques)
static const speed_note_t s_song_butterfly[] = {
    {F_C4, F_C3}, {F_D4, 0}, {F_E4, F_C3}, {F_C4, 0},
    {F_C4, F_C3}, {F_D4, 0}, {F_E4, F_C3}, {F_C4, 0},
    {F_E4, F_C3}, {F_F4, 0}, {F_G4, F_G3},
    {F_E4, F_C3}, {F_F4, 0}, {F_G4, F_G3},
    {F_G4, F_E3}, {F_A4, 0}, {F_G4, 0}, {F_F4, 0}, {F_E4, F_C3}, {F_C4, 0},
    {F_G4, F_E3}, {F_A4, 0}, {F_G4, 0}, {F_F4, 0}, {F_E4, F_C3}, {F_C4, 0},
    {F_C4, F_C3}, {F_G3, F_G2}, {F_C4, F_C3},
    {F_C4, F_C3}, {F_G3, F_G2}, {F_C4, F_C3}
};

// 3. Một Con Vịt (Vịt Xòe Hai Cánh)
static const speed_note_t s_song_duck[] = {
    {F_G4, F_C3}, {F_E4, 0}, {F_G4, F_C3}, {F_E4, 0}, {F_G4, F_C3}, {F_A4, 0}, {F_G4, F_C3},
    {F_G4, F_F3}, {F_A4, 0}, {F_C5, F_F3}, {F_A4, 0}, {F_G4, F_C3}, {F_E4, 0}, {F_D4, F_G2},
    {F_D4, F_G2}, {F_E4, 0}, {F_G4, F_C3}, {F_A4, 0}, {F_G4, F_C3}, {F_E4, 0},
    {F_G4, F_C3}, {F_A4, 0}, {F_G4, F_C3}, {F_E4, 0}, {F_D4, F_G2}, {F_D4, 0}, {F_C4, F_C3}
};

// 4. Chúc Mừng Sinh Nhật (Happy Birthday)
static const speed_note_t s_song_birthday[] = {
    {F_C4, F_C3}, {F_C4, 0}, {F_D4, 0}, {F_C4, F_C3}, {F_F4, F_F3}, {F_E4, 0},
    {F_C4, F_C3}, {F_C4, 0}, {F_D4, 0}, {F_C4, F_C3}, {F_G4, F_C3}, {F_F4, 0},
    {F_C4, F_C3}, {F_C4, 0}, {F_C5, F_F3}, {F_A4, 0}, {F_F4, F_F3}, {F_E4, 0}, {F_D4, F_AS2},
    {F_AS4, F_AS2}, {F_AS4, 0}, {F_A4, 0}, {F_F4, F_F3}, {F_G4, F_C3}, {F_F4, F_F3}
};

// 5. Super Mario Bros (Game Rộn Ràng)
static const speed_note_t s_song_mario[] = {
    {F_E5, F_C3}, {F_E5, F_C3}, {F_E5, F_C3}, {F_C5, F_C3}, {F_E5, F_C3}, {F_G5, F_G3}, {F_G4, F_G2},
    {F_C5, F_E3}, {F_G4, F_C3}, {F_E4, F_G2}, {F_A4, F_A2}, {F_B4, F_B2}, {F_AS4, F_AS2}, {F_A4, F_A2},
    {F_G4, F_E3}, {F_E5, F_G3}, {F_G5, F_C4}, {F_A5, F_F3}, {F_F5, F_D3}, {F_G5, F_E3}, {F_E5, F_C3},
    {F_C5, F_A2}, {F_D5, F_B2}, {F_B4, F_G2}
};

static const speed_song_def_t s_speed_songs[] = {
    { s_song_babyshark, sizeof(s_song_babyshark)/sizeof(s_song_babyshark[0]), "Baby Shark",         "Cá Mập Con (Doo doo doo...)" },
    { s_song_butterfly, sizeof(s_song_butterfly)/sizeof(s_song_butterfly[0]), "Kìa Con Bướm Vàng", "Bướm Bay Tung Tăng" },
    { s_song_duck,      sizeof(s_song_duck)/sizeof(s_song_duck[0]),           "Một Con Vịt",        "Vịt Xòe Hai Cánh" },
    { s_song_birthday,  sizeof(s_song_birthday)/sizeof(s_song_birthday[0]),   "Happy Birthday",     "Chúc Mừng Sinh Nhật" },
    { s_song_mario,     sizeof(s_song_mario)/sizeof(s_song_mario[0]),         "Super Mario",        "Thế Giới Nấm Rộn Rã" }
};
#define SPEED_SONG_COUNT (sizeof(s_speed_songs)/sizeof(s_speed_songs[0]))

static uint8_t  s_speed_song_idx = 0;
static uint16_t s_speed_note_idx = 0;
static int64_t  s_speed_last_key_us = 0;
static uint8_t  s_speed_timbre = TIMBRE_PIANO;

void audio_speed_reset(void) {
    s_speed_note_idx = 0;
    s_speed_last_key_us = 0;
}

bool audio_speed_play_note(speed_note_info_t *out_info) {
    int64_t now = esp_timer_get_time();
    int64_t dt_ms = (s_speed_last_key_us > 0) ? ((now - s_speed_last_key_us) / 1000) : 500;
    s_speed_last_key_us = now;

    uint32_t bpm = 120;
    if (dt_ms > 20 && dt_ms < 2000) {
        bpm = (uint32_t)(60000 / dt_ms);
    }

    // Dynamic articulation based on typing speed: crisp staccato when fast, warm legato when slow
    uint32_t note_dur_ms = 280;
    if (dt_ms <= 160) {
        note_dur_ms = 140;
    } else if (dt_ms > 380) {
        note_dur_ms = 520;
    }

    const speed_song_def_t *curr = &s_speed_songs[s_speed_song_idx];
    speed_note_t n = curr->notes[s_speed_note_idx];

    if (n.freq > 20.0f) {
        audio_play_piano_note_dur(n.freq, s_speed_timbre, 0, note_dur_ms);
    }

    if (n.bass_freq > 20.0f) {
        audio_play_piano_note_dur(n.bass_freq, s_speed_timbre, 0, note_dur_ms + 90);
    }

    uint16_t current_played = s_speed_note_idx;
    s_speed_note_idx++;

    bool finished = false;
    if (s_speed_note_idx >= curr->count) {
        finished = true;
        s_speed_note_idx = 0;
    }

    if (out_info) {
        out_info->song_name = curr->name;
        out_info->title_vi = curr->title_vi;
        out_info->song_idx = s_speed_song_idx;
        out_info->total_songs = SPEED_SONG_COUNT;
        out_info->note_idx = current_played + 1;
        out_info->total_notes = curr->count;
        out_info->freq = n.freq;
        out_info->speed_bpm = bpm;
        out_info->is_finished = finished;
    }
    return finished;
}

void audio_speed_next_song(void) {
    s_speed_song_idx = (s_speed_song_idx + 1) % SPEED_SONG_COUNT;
    s_speed_note_idx = 0;
    const speed_song_def_t *curr = &s_speed_songs[s_speed_song_idx];
    audio_play_piano_note_dur(curr->notes[0].freq, s_speed_timbre, 0, 260);
}

void audio_speed_prev_song(void) {
    s_speed_song_idx = (s_speed_song_idx + SPEED_SONG_COUNT - 1) % SPEED_SONG_COUNT;
    s_speed_note_idx = 0;
    const speed_song_def_t *curr = &s_speed_songs[s_speed_song_idx];
    audio_play_piano_note_dur(curr->notes[0].freq, s_speed_timbre, 0, 260);
}

void audio_speed_select_song(uint8_t song_idx) {
    s_speed_song_idx = song_idx % SPEED_SONG_COUNT;
    s_speed_note_idx = 0;
    const speed_song_def_t *curr = &s_speed_songs[s_speed_song_idx];
    audio_play_piano_note_dur(curr->notes[0].freq, s_speed_timbre, 0, 260);
}

uint8_t audio_speed_toggle_timbre(void) {
    s_speed_timbre = (s_speed_timbre + 1) % 3;
    audio_play_piano_note_dur(F_C5, s_speed_timbre, 0, 260);
    return s_speed_timbre;
}

const char *audio_speed_get_timbre_name(void) {
    switch (s_speed_timbre) {
        case TIMBRE_MUSIC_BOX:  return "Hộp Nhạc Chuông Ngân";
        case TIMBRE_8BIT_MARIO: return "Đàn Điện Tử 8-Bit Mario";
        default:                return "Đàn Grand Piano Diệu Kỳ";
    }
}

void audio_speed_get_info(speed_note_info_t *out_info) {
    if (!out_info) return;
    const speed_song_def_t *curr = &s_speed_songs[s_speed_song_idx];
    out_info->song_name = curr->name;
    out_info->title_vi = curr->title_vi;
    out_info->song_idx = s_speed_song_idx;
    out_info->total_songs = SPEED_SONG_COUNT;
    out_info->note_idx = s_speed_note_idx + 1;
    out_info->total_notes = curr->count;
    out_info->freq = curr->notes[s_speed_note_idx].freq;
    out_info->speed_bpm = 120;
    out_info->is_finished = false;
}

void audio_speed_runner_keystroke(void) {
    audio_speed_play_note(NULL);
}

void audio_speed_runner_idle_check(void) {
}

// ============================================================================
// Mode 4: Drum Kit (with Pitched Drum Variations)
// ============================================================================
void audio_play_drum_pitched(drum_type_t drum, float semitone) {
    if (sb_play_drum_pitched((int)drum, semitone)) return;
    float mult = (semitone == 0.0f) ? 1.0f : powf(2.0f, semitone / 12.0f);
    switch (drum) {
        case DRUM_KICK:
            trigger_voice_internal(VOICE_PITCH_SWEEP, 0, 190.0f * mult, 60.0f * mult, 1.0f, 160, 2);
            break;
        case DRUM_SNARE:
            trigger_voice_internal(VOICE_NOISE_BURST, 0, 0, 0, 0.95f, 160, 1);
            trigger_voice_internal(VOICE_TONE_ADSR, 190.0f * mult, 0, 0, 0.65f, 80, TIMBRE_PIANO);
            break;
        case DRUM_HIHAT:
            trigger_voice_internal(VOICE_NOISE_BURST, 0, 0, 0, 0.6f, 45, 1);
            break;
        case DRUM_CYMBAL:
            trigger_voice_internal(VOICE_NOISE_BURST, 0, 0, 0, 0.7f, 450, 1);
            break;
        case DRUM_TOM:
            trigger_voice_internal(VOICE_PITCH_SWEEP, 0, 300.0f * mult, 140.0f * mult, 0.9f, 200, 1);
            break;
        case DRUM_CLAP:
            trigger_voice_internal(VOICE_NOISE_BURST, 0, 0, 0, 0.9f, 140, 2);
            break;
    }
}

void audio_play_drum(drum_type_t drum) {
    audio_play_drum_pitched(drum, 0.0f);
}

// ============================================================================
// Mode 5: Vintage Typewriter
// ============================================================================
void audio_play_typewriter(typewriter_sound_t type) {
    if (sb_play_typewriter((int)type)) return;
    switch (type) {
        case TYPEWRITER_KEY_CLICK:
            trigger_voice_internal(VOICE_NOISE_BURST, 0, 0, 0, 0.75f, 25, 1);
            break;
        case TYPEWRITER_SPACE_SLIDE:
            trigger_voice_internal(VOICE_NOISE_BURST, 0, 0, 0, 0.5f, 140, 0);
            break;
        case TYPEWRITER_ENTER_BELL:
            trigger_voice_internal(VOICE_TONE_ADSR, 2489.0f, 0, 0, 0.95f, 650, TIMBRE_MUSIC_BOX);
            break;
        case TYPEWRITER_BACKSPACE:
            trigger_voice_internal(VOICE_PITCH_SWEEP, 0, 300.0f, 120.0f, 0.6f, 90, 0);
            break;
    }
}

// ============================================================================
// Mode 6: Cartoon SFX (with pitch & non-blocking delay support)
// ============================================================================
void audio_play_cartoon_sfx_ex(cartoon_sfx_t sfx, float semitone, uint32_t delay_ms) {
    if (sb_play_cartoon_ex((int)sfx, semitone, delay_ms)) return;
    float mult = (semitone == 0.0f) ? 1.0f : powf(2.0f, semitone / 12.0f);
    switch (sfx) {
        case SFX_MARIO_COIN:
            trigger_voice_at(VOICE_TONE_ADSR, 987.77f * mult, 0, 0, 0.85f, 80, TIMBRE_8BIT_MARIO, delay_ms);
            trigger_voice_at(VOICE_TONE_ADSR, 1318.51f * mult, 0, 0, 0.95f, 320, TIMBRE_8BIT_MARIO, delay_ms + 70);
            break;
        case SFX_SPRING_BOING:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 220.0f * mult, 680.0f * mult, 0.9f, 280, 1, delay_ms);
            break;
        case SFX_LASER:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 2600.0f * mult, 180.0f * mult, 0.85f, 130, 0, delay_ms);
            break;
        case SFX_CAR_HORN:
            trigger_voice_at(VOICE_TONE_ADSR, 440.0f * mult, 0, 0, 0.7f, 220, TIMBRE_8BIT_MARIO, delay_ms);
            trigger_voice_at(VOICE_TONE_ADSR, 554.37f * mult, 0, 0, 0.7f, 220, TIMBRE_8BIT_MARIO, delay_ms);
            break;
        case SFX_VICTORY_FANFARE:
            trigger_voice_at(VOICE_TONE_ADSR, 523.25f * mult, 0, 0, 0.85f, 140, TIMBRE_8BIT_MARIO, delay_ms);
            trigger_voice_at(VOICE_TONE_ADSR, 659.25f * mult, 0, 0, 0.85f, 140, TIMBRE_8BIT_MARIO, delay_ms + 120);
            trigger_voice_at(VOICE_TONE_ADSR, 783.99f * mult, 0, 0, 0.85f, 140, TIMBRE_8BIT_MARIO, delay_ms + 240);
            trigger_voice_at(VOICE_TONE_ADSR, 1046.50f * mult, 0, 0, 0.95f, 400, TIMBRE_8BIT_MARIO, delay_ms + 360);
            break;
        case SFX_WHISTLE_SLIDE:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 1200.0f * mult, 280.0f * mult, 0.85f, 350, 0, delay_ms);
            break;
        case SFX_WATER_DROP:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 600.0f * mult, 1900.0f * mult, 0.85f, 90, 0, delay_ms);
            break;
        case SFX_DOORBELL:
            trigger_voice_at(VOICE_TONE_ADSR, 783.99f * mult, 0, 0, 0.9f, 300, TIMBRE_MUSIC_BOX, delay_ms);
            trigger_voice_at(VOICE_TONE_ADSR, 659.25f * mult, 0, 0, 0.9f, 500, TIMBRE_MUSIC_BOX, delay_ms + 260);
            break;
        case SFX_FIRE_SIREN:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 650.0f * mult, 1100.0f * mult, 0.85f, 500, 1, delay_ms);
            break;
        case SFX_SKID_STOP:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 1800.0f * mult, 400.0f * mult, 0.8f, 200, 0, delay_ms);
            break;
        case SFX_GAME_JUMP:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 140.0f * mult, 520.0f * mult, 0.9f, 220, 0, delay_ms);
            break;
        case SFX_GAME_POWERUP:
        case SFX_GAME_1UP:
            trigger_voice_at(VOICE_TONE_ADSR, 659.25f * mult, 0, 0, 0.85f, 100, TIMBRE_8BIT_MARIO, delay_ms);
            trigger_voice_at(VOICE_TONE_ADSR, 1046.50f * mult, 0, 0, 0.90f, 250, TIMBRE_8BIT_MARIO, delay_ms + 90);
            break;
        case SFX_GAME_FIREBALL:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 950.0f * mult, 180.0f * mult, 0.85f, 100, 0, delay_ms);
            break;
        case SFX_GAME_WARP:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 380.0f * mult, 110.0f * mult, 0.85f, 300, 0, delay_ms);
            break;
        case SFX_GAME_BOMB:
        case SFX_GAME_PUNCH:
        case SFX_CT_TIMPANI_BOOM:
            trigger_voice_at(VOICE_NOISE_BURST, 0, 0, 0, 0.95f, 280, DRUM_KICK, delay_ms);
            break;
        case SFX_GAME_PACMAN:
        case SFX_CT_SQUEAK_DUCK:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 420.0f * mult, 140.0f * mult, 0.85f, 120, 0, delay_ms);
            break;
        case SFX_GAME_SONIC_DASH:
        case SFX_CT_RUN_AWAY:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 220.0f * mult, 1600.0f * mult, 0.9f, 320, 0, delay_ms);
            break;
        case SFX_GAME_OVER_TROMBONE:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 310.0f * mult, 180.0f * mult, 0.85f, 550, 0, delay_ms);
            break;
        case SFX_CT_BONGO_RUN:
        case SFX_CT_SLAPSTICK:
            trigger_voice_at(VOICE_NOISE_BURST, 0, 0, 0, 0.85f, 90, DRUM_SNARE, delay_ms);
            break;
        case SFX_CT_SLIP_FALL:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 1800.0f * mult, 250.0f * mult, 0.9f, 300, 0, delay_ms);
            break;
        case SFX_CT_ANVIL_CLANG:
        case SFX_CT_COWBELL:
            trigger_voice_at(VOICE_TONE_ADSR, 1174.66f * mult, 0, 0, 0.95f, 400, TIMBRE_MUSIC_BOX, delay_ms);
            break;
        case SFX_CT_FLEXATONE:
        case SFX_CT_VIBRASLAP:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 450.0f * mult, 950.0f * mult, 0.85f, 350, 1, delay_ms);
            break;
        case SFX_CT_SPLAT:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 450.0f * mult, 120.0f * mult, 0.8f, 160, 0, delay_ms);
            break;
        case SFX_CT_BALL_WHISTLE:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 2400.0f * mult, 2200.0f * mult, 0.85f, 200, 1, delay_ms);
            break;
        case SFX_CT_MAGIC_WAND:
            trigger_voice_at(VOICE_TONE_ADSR, 1567.98f * mult, 0, 0, 0.85f, 150, TIMBRE_MUSIC_BOX, delay_ms);
            trigger_voice_at(VOICE_TONE_ADSR, 2637.02f * mult, 0, 0, 0.90f, 350, TIMBRE_MUSIC_BOX, delay_ms + 90);
            break;
        case SFX_CT_GHOST_BOO:
            trigger_voice_at(VOICE_PITCH_SWEEP, 0, 280.0f * mult, 480.0f * mult, 0.8f, 450, 1, delay_ms);
            break;
        default:
            break;
    }
}

void audio_play_cartoon_sfx(cartoon_sfx_t sfx) {
    audio_play_cartoon_sfx_ex(sfx, 0.0f, 0);
}

// ============================================================================
// Mode 7: Morse Code & Robot (with Anti-Drone Voice Choking & Melodic Pitch)
// ============================================================================
static const char *s_morse_table[26] = {
    ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....", "..",
    ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.", "--.-", ".-.",
    "...",  "-",    "..-",  "...-", ".--",  "-..-", "-.--", "--.."
};

static const char *s_morse_digits_tbl[10] = {
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
};

void audio_play_morse_char(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    const char *morse = NULL;
    float morse_freq = 750.0f;
    if (c >= 'A' && c <= 'Z') {
        morse = s_morse_table[c - 'A'];
        morse_freq = 580.0f + (float)(c - 'A') * 18.0f; // Distinct musical pitch per letter (580..1030 Hz)
    } else if (c >= '0' && c <= '9') {
        morse = s_morse_digits_tbl[c - '0'];
        morse_freq = 640.0f + (float)(c - '0') * 35.0f;
    } else {
        return;
    }

    // Cancel any previously scheduled/active Morse dits/dahs so rapid typing never drones
    if (s_voice_mutex && xSemaphoreTake(s_voice_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (int i = 0; i < MAX_POLY_VOICES; i++) {
            if (s_voices[i].active && s_voices[i].mode == VOICE_MORSE) {
                s_voices[i].active = false;
            }
        }
        xSemaphoreGive(s_voice_mutex);
    }

    uint32_t t = 0;
    while (*morse) {
        uint32_t dur = (*morse == '.') ? 65 : 195;
        trigger_voice_at(VOICE_MORSE, morse_freq, 0, 0, 0.8f, dur, 0, t);
        t += dur + 60;
        morse++;
    }
}

void audio_play_robot_chirp(void) {
    int num_chirps = 3 + (rand() % 3);
    uint32_t t = 0;
    for (int i = 0; i < num_chirps; i++) {
        float f = 1400.0f + (rand() % 1800);
        float f_end = f * (0.55f + (float)(rand() % 130) / 100.0f);
        uint32_t dur = 60 + rand() % 60;
        trigger_voice_at(VOICE_R2D2, 0, f, f_end, 0.7f, dur, 0, t);
        t += dur + 25;
    }
}

// ============================================================================
// Mode 8: Cockpit & Animals
// ============================================================================
void audio_cockpit_engine_start(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_ENGINE)) return;
    if (sb_play_engine()) return;
    trigger_voice_internal(VOICE_ENGINE_RUMBLE, 80.0f, 0, 0, 0.95f, 700, 0);
}

void audio_cockpit_engine_stop(void) {}

void audio_cockpit_siren_set(bool on) {
    s_siren_on = on;
    if (sb_has_siren()) {
        sb_siren_loop(s_siren_on);
    }
}

void audio_cockpit_siren_toggle(void) {
    audio_cockpit_siren_set(!s_siren_on);
    ESP_LOGI(TAG, "Cockpit Siren: %s", s_siren_on ? "ON" : "OFF");
}

bool audio_cockpit_is_siren_on(void) {
    return s_siren_on;
}

void audio_cockpit_rocket_launch(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_ROCKET)) return;
    if (sb_play_rocket()) return;
    trigger_voice_internal(VOICE_TONE_ADSR, 880.0f, 0, 0, 0.9f, 250, TIMBRE_8BIT_MARIO);
    trigger_voice_internal(VOICE_PITCH_SWEEP, 0, 120.0f, 700.0f, 1.0f, 800, 1);
}

void audio_cockpit_water_hose(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_WATER)) return;
    trigger_voice_internal(VOICE_PITCH_SWEEP, 0, 600.0f, 1800.0f, 0.9f, 600, 0);
}

void audio_cockpit_horn(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_HORN)) return;
    audio_play_cartoon_sfx(SFX_CAR_HORN);
}

void audio_cockpit_skid(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_SKID)) return;
    audio_play_cartoon_sfx(SFX_SKID_STOP);
}

void audio_cockpit_bell(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_BELL)) return;
    trigger_voice_internal(VOICE_TONE_ADSR, 1200.0f, 0, 0, 0.9f, 400, TIMBRE_MUSIC_BOX);
}

void audio_cockpit_reverse(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_REVERSE)) return;
    trigger_voice_internal(VOICE_TONE_ADSR, 1250.0f, 0, 0, 0.8f, 200, TIMBRE_8BIT_MARIO);
}

void audio_cockpit_turn(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_TURN)) return;
    trigger_voice_internal(VOICE_TONE_ADSR, 2200.0f, 0, 0, 0.6f, 40, TIMBRE_PIANO);
}

void audio_cockpit_radio(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_RADIO)) return;
    audio_play_robot_chirp();
}

void audio_cockpit_cheer(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_CHEER)) return;
    audio_play_cartoon_sfx(SFX_VICTORY_FANFARE);
}

void audio_cockpit_fire(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_FIRE)) return;
}

void audio_cockpit_helicopter(void) {
    if (sb_play_cockpit_sfx(COCKPIT_SFX_HELI)) return;
    trigger_voice_internal(VOICE_ENGINE_RUMBLE, 45.0f, 0, 0, 0.9f, 800, 0);
}

void audio_cockpit_play_sfx_delayed(int sfx_id, uint32_t delay_ms) {
    sb_play_cockpit_sfx_delayed(sfx_id, delay_ms);
}

void audio_cockpit_play_sfx(int sfx_id) {
    sb_play_cockpit_sfx_delayed(sfx_id, 0);
}

void audio_play_animal_sound(char key) {
    if (key >= 'a' && key <= 'z') key -= 32;
    if (sb_play_animal(key)) return;

    switch (key) {
        case 'D':
            trigger_voice_internal(VOICE_ANIMAL, 0, 280.0f, 150.0f, 0.9f, 160, 0);
            break;
        case 'C':
            trigger_voice_internal(VOICE_ANIMAL, 0, 450.0f, 900.0f, 0.9f, 350, 0);
            break;
        case 'B':
            trigger_voice_internal(VOICE_ANIMAL, 0, 2800.0f, 3800.0f, 0.9f, 100, 0);
            break;
        case 'M':
            trigger_voice_internal(VOICE_ANIMAL, 0, 200.0f, 150.0f, 0.9f, 500, 0);
            break;
        case 'Q':
            trigger_voice_internal(VOICE_ANIMAL, 0, 340.0f, 260.0f, 0.9f, 200, 0);
            break;
        case 'F':
            trigger_voice_internal(VOICE_ANIMAL, 0, 200.0f, 130.0f, 0.9f, 180, 0);
            break;
        case 'L':
            trigger_voice_internal(VOICE_ANIMAL, 0, 240.0f, 100.0f, 0.9f, 600, 0);
            break;
        case 'P':
            trigger_voice_internal(VOICE_ANIMAL, 0, 400.0f, 250.0f, 0.9f, 150, 0);
            break;
        default:
            trigger_voice_internal(VOICE_ANIMAL, 0, 400.0f + (key % 10) * 80.0f, 800.0f, 0.9f, 200, 0);
            break;
    }
}

// ============================================================================
// Mode 9: Phonics & Counting Learning (Synchronized Speech + Drum Beats + LEDs)
// ============================================================================
void audio_play_counting_number(uint8_t num) {
    if (num > 10) num = 10;

    uint32_t speech_dur_ms = 550;
    bool has_speech = sb_play_number(num, &speech_dur_ms);
    if (!has_speech) {
        trigger_voice_internal(VOICE_TONE_ADSR, 261.63f + num * 32.0f, 0, 0, 0.9f, 280, TIMBRE_PIANO);
        speech_dur_ms = 300;
    }

    if (num == 0) {
        led_effects_color_burst(0, 200, 255);
        return;
    }

    // Schedule rhythmic counting beats + synchronized LEDs right after spoken number finishes
    uint32_t start_delay_ms = speech_dur_ms + 80;
    uint32_t step_ms = 210;
    for (uint8_t i = 0; i < num; i++) {
        uint32_t beat_delay = start_delay_ms + (uint32_t)i * step_ms;
        if (!sb_play_beat(beat_delay)) {
            float f = 330.0f + (float)i * 40.0f;
            trigger_voice_at(VOICE_TONE_ADSR, f, 0, 0, 0.70f, 110, TIMBRE_MUSIC_BOX, beat_delay);
        }
    }
    led_effects_start_counting(num, start_delay_ms, step_ms);
}

void audio_play_phonics_letter(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    if (sb_play_letter(c)) return;

    float f1 = 600.0f;
    float f2 = 1200.0f;

    switch (c) {
        case 'A': f1 = 800.0f; f2 = 1250.0f; break;
        case 'E': f1 = 530.0f; f2 = 1840.0f; break;
        case 'I': f1 = 270.0f; f2 = 2290.0f; break;
        case 'O': f1 = 510.0f; f2 = 840.0f;  break;
        case 'U': f1 = 300.0f; f2 = 870.0f;  break;
        case 'B': f1 = 300.0f; f2 = 1100.0f; break;
        case 'C': f1 = 400.0f; f2 = 1400.0f; break;
        case 'D': f1 = 350.0f; f2 = 1600.0f; break;
        case 'G': f1 = 380.0f; f2 = 1300.0f; break;
        case 'M': f1 = 280.0f; f2 = 1000.0f; break;
        case 'N': f1 = 320.0f; f2 = 1450.0f; break;
        case 'P': f1 = 340.0f; f2 = 1200.0f; break;
        case 'T': f1 = 400.0f; f2 = 1700.0f; break;
        case 'V': f1 = 320.0f; f2 = 1500.0f; break;
        default:  f1 = 400.0f + (c % 7) * 60.0f; f2 = 1100.0f + (c % 9) * 80.0f; break;
    }

    trigger_voice_internal(VOICE_TONE_ADSR, f1, 0, 0, 0.75f, 220, TIMBRE_PIANO);
    trigger_voice_internal(VOICE_TONE_ADSR, f2, 0, 0, 0.45f, 220, TIMBRE_PIANO);
    trigger_voice_internal(VOICE_NOISE_BURST, 0, 0, 0, 0.4f, 30, 0);
}

void audio_play_special_key(uint8_t kc) {
    if (sb_play_special_key(kc)) return;
    trigger_voice_internal(VOICE_TONE_ADSR, 523.25f, 0, 0, 0.70f, 180, TIMBRE_MUSIC_BOX);
}

void audio_play_symbol(char c) {
    if (sb_play_symbol(c)) return;
    trigger_voice_internal(VOICE_TONE_ADSR, 659.25f, 0, 0, 0.70f, 180, TIMBRE_MUSIC_BOX);
}

// ============================================================================
// POWER MANAGEMENT: ÂM THANH TẠM BIỆT, ĐÁNH THỨC & TIẾT KIỆM NĂNG LƯỢNG I2S
// ============================================================================

void audio_play_goodbye_lullaby(void) {
    if (s_is_muted) return;
    // Giai điệu tạm biệt êm dịu Hộp Nhạc (Music Box 4 nốt ru ngủ du dương: E5 -> D5 -> C5 -> G4)
    trigger_voice_internal(VOICE_TONE_ADSR, 659.25f, 0, 0, 0.85f, 260, TIMBRE_MUSIC_BOX);
    vTaskDelay(pdMS_TO_TICKS(220));
    trigger_voice_internal(VOICE_TONE_ADSR, 587.33f, 0, 0, 0.80f, 260, TIMBRE_MUSIC_BOX);
    vTaskDelay(pdMS_TO_TICKS(220));
    trigger_voice_internal(VOICE_TONE_ADSR, 523.25f, 0, 0, 0.85f, 300, TIMBRE_MUSIC_BOX);
    vTaskDelay(pdMS_TO_TICKS(260));
    trigger_voice_internal(VOICE_TONE_ADSR, 392.00f, 0, 0, 0.90f, 550, TIMBRE_MUSIC_BOX);
    vTaskDelay(pdMS_TO_TICKS(580)); // Đợi nốt ngân kết thúc mượt mà trước khi ngắt clock
}

void audio_play_wake_chime(void) {
    if (s_is_muted) return;
    // Chime đánh thức vui tươi: C5 (523Hz) -> G5 (784Hz) -> C6 (1046Hz)
    trigger_voice_internal(VOICE_TONE_ADSR, 523.25f, 0, 0, 0.70f, 80, TIMBRE_MUSIC_BOX);
    vTaskDelay(pdMS_TO_TICKS(60));
    trigger_voice_internal(VOICE_TONE_ADSR, 783.99f, 0, 0, 0.80f, 90, TIMBRE_MUSIC_BOX);
    vTaskDelay(pdMS_TO_TICKS(65));
    trigger_voice_internal(VOICE_TONE_ADSR, 1046.50f, 0, 0, 0.90f, 200, TIMBRE_MUSIC_BOX);
}

void audio_synth_enter_low_power(void) {
    if (!s_audio_ready || s_low_power_mode) return;
    s_low_power_mode = true;
    vTaskDelay(pdMS_TO_TICKS(60)); // Chờ buffer đang phát dở hoàn thành
    if (s_tx_handle) {
        i2s_channel_disable(s_tx_handle);
    }
    ESP_LOGI(TAG, "I2S MAX98357A clock disabled -> IC enters shutdown (<10uA)");
}

void audio_synth_exit_low_power(void) {
    if (!s_audio_ready || !s_low_power_mode) return;
    if (s_tx_handle) {
        i2s_channel_enable(s_tx_handle);
    }
    s_low_power_mode = false;
    ESP_LOGI(TAG, "I2S MAX98357A clock restored -> Ready to play");
}

bool audio_synth_is_low_power(void) {
    return s_low_power_mode;
}

// ============================================================================
// STREAMING AUDIO API (MP3 / WAV PLAYBACK FROM USB FLASH DRIVE)
// ============================================================================

esp_err_t audio_stream_start(uint32_t sample_rate, uint8_t channels) {
    if (!s_stream_ringbuf) {
        s_stream_ringbuf = (int16_t *)heap_caps_malloc(STREAM_RINGBUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_stream_ringbuf) {
            ESP_LOGE(TAG, "Failed to allocate audio stream ringbuf in PSRAM");
            return ESP_ERR_NO_MEM;
        }
    }
    portENTER_CRITICAL(&s_stream_mux);
    s_stream_wr_idx = 0;
    s_stream_rd_idx = 0;
    s_stream_available = 0;
    s_stream_sample_rate = sample_rate ? sample_rate : 44100;
    s_stream_channels = (channels == 1) ? 1 : 2;
    s_stream_resample_ratio = (float)s_stream_sample_rate / (float)AUDIO_SAMPLE_RATE;
    s_stream_resample_frac = 0.0f;
    s_stream_current_energy = 0.0f;
    s_stream_prebuffering = true;
    s_stream_active = true;
    portEXIT_CRITICAL(&s_stream_mux);

    // Cập nhật bộ lọc Hi-Fi DSP Biquad theo tần số lấy mẫu thực tế của bài nhạc
    float fs = (float)s_stream_sample_rate;
    biquad_calc_hpf(&s_dsp_subsonic_l, 75.0f, fs);
    biquad_calc_hpf(&s_dsp_subsonic_r, 75.0f, fs);
    biquad_calc_peaking(&s_dsp_bass_l, 140.0f, fs, 3.0f, 1.0f);
    biquad_calc_peaking(&s_dsp_bass_r, 140.0f, fs, 3.0f, 1.0f);
    biquad_calc_peaking(&s_dsp_treble_l, 4500.0f, fs, 2.2f, 0.8f);
    biquad_calc_peaking(&s_dsp_treble_r, 4500.0f, fs, 2.2f, 0.8f);

    ESP_LOGI(TAG, "Audio stream started: %u Hz, %d ch (Prebuffer: %d, Hi-Fi DSP: Subsonic 75Hz + Bass + Treble, MonoDownmix: %d)",
             (unsigned int)sample_rate, (int)channels, STREAM_PREBUFFER_THRESHOLD, s_dsp_mono_downmix);
    return ESP_OK;
}

void audio_stream_stop(void) {
    portENTER_CRITICAL(&s_stream_mux);
    s_stream_active = false;
    s_stream_prebuffering = true;
    s_stream_wr_idx = 0;
    s_stream_rd_idx = 0;
    s_stream_available = 0;
    s_stream_current_energy = 0.0f;
    s_stream_resample_frac = 0.0f;
    portEXIT_CRITICAL(&s_stream_mux);

    biquad_reset(&s_dsp_subsonic_l);
    biquad_reset(&s_dsp_subsonic_r);
    biquad_reset(&s_dsp_bass_l);
    biquad_reset(&s_dsp_bass_r);
    biquad_reset(&s_dsp_treble_l);
    biquad_reset(&s_dsp_treble_r);

    ESP_LOGI(TAG, "Audio stream stopped");
}

void audio_stream_set_prebuffering(bool enable) {
    portENTER_CRITICAL(&s_stream_mux);
    s_stream_prebuffering = enable;
    portEXIT_CRITICAL(&s_stream_mux);
}

size_t audio_stream_get_available(void) {
    portENTER_CRITICAL(&s_stream_mux);
    size_t avail = s_stream_available;
    portEXIT_CRITICAL(&s_stream_mux);
    return avail;
}

bool audio_stream_is_active(void) {
    return s_stream_active;
}

float audio_stream_get_energy(void) {
    return s_stream_current_energy;
}

size_t audio_stream_write(const int16_t *pcm, size_t num_samples) {
    if (!s_stream_active || !s_stream_ringbuf || !pcm || num_samples == 0) return 0;

    size_t written = 0;
    while (written < num_samples && s_stream_active) {
        portENTER_CRITICAL(&s_stream_mux);
        size_t free_space = STREAM_RINGBUF_SAMPLES - s_stream_available;
        size_t to_write = num_samples - written;
        if (to_write > free_space) to_write = free_space;

        for (size_t i = 0; i < to_write; i++) {
            s_stream_ringbuf[s_stream_wr_idx++] = pcm[written++];
            if (s_stream_wr_idx >= STREAM_RINGBUF_SAMPLES) s_stream_wr_idx = 0;
        }
        s_stream_available += to_write;
        portEXIT_CRITICAL(&s_stream_mux);

        if (written < num_samples) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    return written;
}

// ============================================================================
// HI-FI DSP CONTROL APIS
// ============================================================================

void audio_dsp_set_enabled(bool enabled) {
    s_dsp_enabled = enabled;
    ESP_LOGI(TAG, "Hi-Fi DSP %s", enabled ? "ENABLED" : "DISABLED");
}

bool audio_dsp_is_enabled(void) {
    return s_dsp_enabled;
}

void audio_dsp_set_mono_downmix(bool enable) {
    s_dsp_mono_downmix = enable;
    ESP_LOGI(TAG, "Smart Mono Downmix %s", enable ? "ENABLED" : "DISABLED");
}

bool audio_dsp_is_mono_downmix(void) {
    return s_dsp_mono_downmix;
}
