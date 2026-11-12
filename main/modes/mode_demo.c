#include "mode_demo.h"
#include "audio_synth.h"
#include "soundbank.h"
#include "led_effects.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "MODE_DEMO";

static bool     s_demo_active = false;
static uint8_t  s_demo_mode = 0;
static uint32_t s_demo_elapsed_ms = 0;
static uint32_t s_demo_step_timer = 0;
static uint16_t s_demo_note_idx = 0;
static uint8_t  s_demo_subphase = 0;

void mode_demo_init(void) {
    s_demo_active = false;
    s_demo_mode = 0;
    s_demo_elapsed_ms = 0;
    s_demo_step_timer = 0;
    s_demo_note_idx = 0;
    s_demo_subphase = 0;
}

void mode_demo_start(uint8_t mode_index) {
    s_demo_active = true;
    s_demo_mode = mode_index;
    s_demo_elapsed_ms = 0;
    s_demo_step_timer = 0;
    s_demo_note_idx = 0;
    s_demo_subphase = 0;

    printf("\r\n>>> [KÍCH HOẠT BẢN DEMO 30S ĐẶC SẮC CHO CHẾ ĐỘ %d] <<<\r\n", mode_index + 1);
    printf(">>> (Bé có thể gõ phím bất kỳ lúc nào để bắt đầu chơi tự do!)\r\n");

    if (mode_index == 1) { // Mode 2: Baby Mozart
        audio_mozart_set_autoplay(true);
    }
}

void mode_demo_stop(void) {
    if (!s_demo_active) return;
    s_demo_active = false;

    if (s_demo_mode == 1) {
        audio_mozart_set_autoplay(false);
    } else if (s_demo_mode == 7) {
        audio_cockpit_siren_set(false);
    }
    audio_synth_stop_all_background();

    printf("\r\n>>> [DEMO 30S ĐÃ DỪNG: BÉ BẮT ĐẦU CHƠI TỰ DO] <<<\r\n");
}

bool mode_demo_is_active(void) {
    return s_demo_active;
}

// ----------------------------------------------------------------------------
// MODE 1 DEMO: ĐÀN PIANO DIỆU KỲ & NGŨ CUNG (Concert Piano Suite)
// ----------------------------------------------------------------------------
typedef struct {
    float freq;
    float bass_freq;
    uint16_t dur_ms;
    uint8_t row_side;
    uint8_t pos;
} piano_demo_note_t;

// Medley: Twinkle Twinkle + Für Elise + Ode to Joy + Finale Chord
static const piano_demo_note_t s_piano_demo_notes[] = {
    // 1. Twinkle Twinkle Little Star (0 - 8s)
    { 261.63f, 130.81f, 500, 1, 0 }, // C4 + C3
    { 261.63f, 0.0f,    500, 1, 0 }, // C4
    { 392.00f, 196.00f, 500, 1, 3 }, // G4 + G3
    { 392.00f, 0.0f,    500, 1, 3 }, // G4
    { 440.00f, 220.00f, 500, 1, 4 }, // A4 + A3
    { 440.00f, 0.0f,    500, 1, 4 }, // A4
    { 392.00f, 196.00f, 950, 1, 3 }, // G4
    { 349.23f, 174.61f, 500, 1, 2 }, // F4 + F3
    { 349.23f, 0.0f,    500, 1, 2 }, // F4
    { 329.63f, 164.81f, 500, 1, 1 }, // E4 + E3
    { 329.63f, 0.0f,    500, 1, 1 }, // E4
    { 293.66f, 146.83f, 500, 1, 1 }, // D4 + D3
    { 293.66f, 0.0f,    500, 1, 1 }, // D4
    { 261.63f, 130.81f, 950, 1, 0 }, // C4 + C3

    // 2. Für Elise (Beethoven) (8 - 16s)
    { 659.25f, 0.0f,    320, 0, 1 }, // E5
    { 622.25f, 0.0f,    320, 0, 1 }, // D#5
    { 659.25f, 0.0f,    320, 0, 1 }, // E5
    { 622.25f, 0.0f,    320, 0, 1 }, // D#5
    { 659.25f, 0.0f,    320, 0, 1 }, // E5
    { 493.88f, 0.0f,    320, 0, 0 }, // B4
    { 587.33f, 0.0f,    320, 0, 1 }, // D5
    { 523.25f, 0.0f,    320, 0, 0 }, // C5
    { 440.00f, 110.00f, 750, 1, 4 }, // A4 + A2
    { 261.63f, 0.0f,    250, 1, 0 }, // C4
    { 329.63f, 0.0f,    250, 1, 1 }, // E4
    { 440.00f, 0.0f,    250, 1, 4 }, // A4
    { 493.88f, 123.47f, 750, 0, 0 }, // B4 + B2
    { 329.63f, 0.0f,    250, 1, 1 }, // E4
    { 415.30f, 0.0f,    250, 1, 3 }, // G#4
    { 493.88f, 0.0f,    250, 0, 0 }, // B4
    { 523.25f, 130.81f, 750, 0, 0 }, // C5 + C3

    // 3. Ode to Joy (Beethoven) (16 - 25s)
    { 329.63f, 130.81f, 420, 1, 1 }, // E4 + C3
    { 329.63f, 0.0f,    420, 1, 1 }, // E4
    { 349.23f, 0.0f,    420, 1, 2 }, // F4
    { 392.00f, 196.00f, 420, 1, 3 }, // G4 + G3
    { 392.00f, 0.0f,    420, 1, 3 }, // G4
    { 349.23f, 0.0f,    420, 1, 2 }, // F4
    { 329.63f, 164.81f, 420, 1, 1 }, // E4 + E3
    { 293.66f, 146.83f, 420, 1, 1 }, // D4 + D3
    { 261.63f, 130.81f, 420, 1, 0 }, // C4 + C3
    { 261.63f, 0.0f,    420, 1, 0 }, // C4
    { 293.66f, 146.83f, 420, 1, 1 }, // D4 + D3
    { 329.63f, 164.81f, 420, 1, 1 }, // E4 + E3
    { 329.63f, 0.0f,    550, 1, 1 }, // E4
    { 293.66f, 146.83f, 320, 1, 1 }, // D4 + D3
    { 293.66f, 0.0f,    750, 1, 1 }, // D4

    // 4. Grand Finale Arpeggio & Chord (25 - 28s)
    { 261.63f, 130.81f, 250, 1, 0 }, // C4 + C3
    { 329.63f, 0.0f,    250, 1, 1 }, // E4
    { 392.00f, 0.0f,    250, 1, 3 }, // G4
    { 523.25f, 0.0f,    250, 0, 0 }, // C5
    { 659.25f, 0.0f,    250, 0, 1 }, // E5
    { 783.99f, 0.0f,    250, 0, 3 }, // G5
    { 1046.50f, 130.81f, 1400, 0, 5 } // C6 + C3 Grand Sustain Chord!
};
#define PIANO_DEMO_COUNT (sizeof(s_piano_demo_notes) / sizeof(s_piano_demo_notes[0]))

static void tick_piano_demo(uint32_t delta_ms) {
    if (s_demo_step_timer > delta_ms) {
        s_demo_step_timer -= delta_ms;
        return;
    }
    if (s_demo_note_idx >= PIANO_DEMO_COUNT) {
        return;
    }

    const piano_demo_note_t *n = &s_piano_demo_notes[s_demo_note_idx];
    s_demo_step_timer = n->dur_ms;
    s_demo_note_idx++;

    if (!sb_play_piano_dur(n->freq, TIMBRE_PIANO, 0, n->dur_ms + 120)) {
        audio_play_piano_note_dur(n->freq, TIMBRE_PIANO, 0, n->dur_ms + 80);
    }
    if (n->bass_freq > 20.0f) {
        if (!sb_play_piano_dur(n->bass_freq, TIMBRE_PIANO, 0, n->dur_ms + 250)) {
            audio_play_piano_note_dur(n->bass_freq, TIMBRE_PIANO, 0, n->dur_ms + 180);
        }
    }
    led_effects_piano_key(n->row_side, n->pos, TIMBRE_PIANO, 0, 220, 255);
}

// ----------------------------------------------------------------------------
// MODE 3 DEMO: NHẠC CHẠY THEO TỐC ĐỘ (Speed Melody Super Mario Nitro)
// ----------------------------------------------------------------------------
static const float s_mario_melody[] = {
    659.25f, 659.25f, 0.0f, 659.25f, 0.0f, 523.25f, 659.25f, 0.0f, 783.99f, 0.0f, 0.0f, 392.00f,
    523.25f, 0.0f, 392.00f, 0.0f, 329.63f, 0.0f, 440.00f, 493.88f, 466.16f, 440.00f,
    392.00f, 659.25f, 783.99f, 880.00f, 698.46f, 783.99f, 659.25f, 523.25f, 587.33f, 493.88f
};
#define MARIO_MELODY_LEN (sizeof(s_mario_melody) / sizeof(s_mario_melody[0]))

static void tick_speed_demo(uint32_t delta_ms) {
    if (s_demo_step_timer > delta_ms) {
        s_demo_step_timer -= delta_ms;
        return;
    }

    // Tốc độ tăng dần theo thời gian (BPM 100 -> 160 -> 220)
    uint16_t step_ms = 220;
    uint8_t speed_lvl = 1;
    if (s_demo_elapsed_ms > 18000) {
        step_ms = 110; // Super Nitro
        speed_lvl = 3;
    } else if (s_demo_elapsed_ms > 9000) {
        step_ms = 150; // Fast
        speed_lvl = 2;
    }
    s_demo_step_timer = step_ms;

    float freq = s_mario_melody[s_demo_note_idx % MARIO_MELODY_LEN];
    s_demo_note_idx++;

    if (freq > 20.0f) {
        audio_play_piano_note_dur(freq, TIMBRE_8BIT_MARIO, 0, step_ms + 60);
        led_effects_speedometer(speed_lvl, (float)s_demo_elapsed_ms / 30000.0f);
    }
}

// ----------------------------------------------------------------------------
// MODE 4 DEMO: DÀN TRỐNG BEAT-MAKER & DJ (125 BPM Pumping Groove)
// ----------------------------------------------------------------------------
static void tick_drum_demo(uint32_t delta_ms) {
    if (s_demo_step_timer > delta_ms) {
        s_demo_step_timer -= delta_ms;
        return;
    }
    // 125 BPM: mỗi 16th-note = 120ms
    s_demo_step_timer = 120;
    uint8_t beat_16 = (uint8_t)(s_demo_note_idx % 16);
    s_demo_note_idx++;

    bool kick = (beat_16 == 0 || beat_16 == 8 || beat_16 == 10);
    bool snare = (beat_16 == 4 || beat_16 == 12);
    bool hihat = (beat_16 % 2 == 0);
    bool cymbal = (beat_16 == 0 && (s_demo_note_idx % 32 == 0));
    bool clap = (beat_16 == 4 || beat_16 == 12);
    bool tom = (beat_16 == 14 || beat_16 == 15);

    if (kick) {
        sb_play_drum_pitched(DRUM_KICK, 0.0f);
        led_effects_drum_kick(0);
    }
    if (snare) {
        sb_play_drum_pitched(DRUM_SNARE, 0.0f);
        led_effects_drum_snare(0);
    }
    if (hihat) {
        sb_play_drum_pitched(DRUM_HIHAT, (beat_16 % 4 == 0) ? 2.0f : 0.0f);
    }
    if (clap) {
        sb_play_drum_pitched(DRUM_CLAP, 0.0f);
    }
    if (tom) {
        sb_play_drum_pitched(DRUM_TOM, (float)(beat_16 - 12) * 2.0f);
    }
    if (cymbal) {
        sb_play_drum_pitched(DRUM_CYMBAL, 0.0f);
        led_effects_color_burst(255, 220, 0);
    }
    led_effects_beat();
}

// ----------------------------------------------------------------------------
// MODE 5 DEMO: MÁY GÕ CHỮ CỔ ĐIỂN (Leroy Anderson's The Typewriter)
// ----------------------------------------------------------------------------
static void tick_typewriter_demo(uint32_t delta_ms) {
    if (s_demo_step_timer > delta_ms) {
        s_demo_step_timer -= delta_ms;
        return;
    }

    uint8_t col = (uint8_t)(s_demo_note_idx % 12);
    s_demo_note_idx++;

    if (col < 8) {
        // Nhịp gõ chữ nhanh ta-ta-ta-ta
        s_demo_step_timer = 110;
        sb_play_typewriter(TYPEWRITER_KEY_CLICK);
        led_effects_carriage(col, 8);
    } else if (col == 8) {
        // Ding! Chuông báo hết dòng
        s_demo_step_timer = 280;
        sb_play_typewriter(TYPEWRITER_ENTER_BELL);
        led_effects_color_burst(255, 230, 80);
    } else if (col == 9) {
        // Zwing-clack! Cần gạt đẩy giấy về đầu dòng
        s_demo_step_timer = 450;
        sb_play_typewriter(TYPEWRITER_SPACE_SLIDE);
        led_effects_carriage(0, 8);
    } else {
        // Khoảng lặng ngắn trước khi gõ dòng tiếp theo
        s_demo_step_timer = 200;
    }
}

// ----------------------------------------------------------------------------
// MODE 6 DEMO: HỘP ÂM THANH HOẠT HÌNH & GAME SFX (Cartoon Comedy Medley)
// ----------------------------------------------------------------------------
static const struct {
    cartoon_sfx_t sfx;
    uint16_t dur_ms;
} s_cartoon_show[] = {
    { SFX_SPRING_BOING,       1600 },
    { SFX_MARIO_COIN,         1400 },
    { SFX_WHISTLE_SLIDE,      1800 },
    { SFX_LASER,              1400 },
    { SFX_CT_SLIP_FALL,       1800 },
    { SFX_CT_ANVIL_CLANG,     1600 },
    { SFX_CT_MAGIC_WAND,      1700 },
    { SFX_CT_SQUEAK_DUCK,     1500 },
    { SFX_CT_BONGO_RUN,       1700 },
    { SFX_GAME_PACMAN,        1800 },
    { SFX_GAME_OVER_TROMBONE, 2200 },
    { SFX_GAME_1UP,           1200 },
    { SFX_VICTORY_FANFARE,    2500 }
};
#define CARTOON_SHOW_LEN (sizeof(s_cartoon_show) / sizeof(s_cartoon_show[0]))

static void tick_cartoon_demo(uint32_t delta_ms) {
    if (s_demo_step_timer > delta_ms) {
        s_demo_step_timer -= delta_ms;
        return;
    }
    if (s_demo_note_idx >= CARTOON_SHOW_LEN) {
        s_demo_note_idx = 0;
    }

    cartoon_sfx_t sfx = s_cartoon_show[s_demo_note_idx].sfx;
    s_demo_step_timer = s_cartoon_show[s_demo_note_idx].dur_ms;
    s_demo_note_idx++;

    sb_play_cartoon(sfx);
    led_effects_color_burst(255, 100, 200);
}

// ----------------------------------------------------------------------------
// MODE 7 DEMO: ĐIỆP VIÊN NHÍ & ROBOT R2-D2 (Mission Impossible & Sci-Fi)
// ----------------------------------------------------------------------------
static const float s_spy_bass[] = {
    98.00f, 0.0f, 98.00f, 0.0f, 116.54f, 130.81f, // G2, G2, Bb2, C3
    98.00f, 0.0f, 98.00f, 0.0f, 87.31f,  92.50f   // G2, G2, F2, F#2
};
#define SPY_BASS_LEN (sizeof(s_spy_bass) / sizeof(s_spy_bass[0]))

static void tick_agent_demo(uint32_t delta_ms) {
    if (s_demo_step_timer > delta_ms) {
        s_demo_step_timer -= delta_ms;
        return;
    }
    s_demo_step_timer = 280;

    float f = s_spy_bass[s_demo_note_idx % SPY_BASS_LEN];
    s_demo_note_idx++;

    if (f > 20.0f) {
        audio_play_piano_note_dur(f, TIMBRE_8BIT_MARIO, 0, 220);
    }

    // Mỗi 4 giây phát âm R2-D2 hoặc tín hiệu Morse
    if (s_demo_note_idx % 14 == 0) {
        audio_play_robot_chirp();
        led_effects_searchlight();
    } else {
        led_effects_square_side_pulse(s_demo_note_idx % 4, 0, 255, 180);
    }
}

// ----------------------------------------------------------------------------
// MODE 8 DEMO: BUỒNG LÁI XE CỨU HỎA & 26 CON THÚ (Emergency & Safari Tour)
// ----------------------------------------------------------------------------
static void tick_cockpit_demo(uint32_t delta_ms) {
    (void)delta_ms;
    uint32_t t = s_demo_elapsed_ms;

    if (t >= 1400 && t < 1600 && s_demo_subphase == 0) {
        s_demo_subphase = 1;
        sb_play_engine(); // Nổ máy xe cứu hỏa
        led_effects_color_burst(255, 120, 0);
    } else if (t >= 4000 && t < 4200 && s_demo_subphase == 1) {
        s_demo_subphase = 2;
        audio_cockpit_siren_set(true); // Bật còi báo động cứu hỏa
    } else if (t >= 7500 && t < 7700 && s_demo_subphase == 2) {
        s_demo_subphase = 3;
        audio_cockpit_horn(); // Bóp còi xe Bim-bim!
    } else if (t >= 9500 && t < 9700 && s_demo_subphase == 3) {
        s_demo_subphase = 4;
        audio_cockpit_skid(); // Phanh gấp ken két
    } else if (t >= 12000 && t < 12200 && s_demo_subphase == 4) {
        s_demo_subphase = 5;
        audio_cockpit_siren_set(false); // Tắt còi, trực thăng cứu hộ tới
        audio_cockpit_play_sfx(COCKPIT_SFX_HELI);
    } else if (t >= 15000 && t < 15200 && s_demo_subphase == 5) {
        s_demo_subphase = 6;
        sb_play_animal('d'); // Chó sủa
    } else if (t >= 17000 && t < 17200 && s_demo_subphase == 6) {
        s_demo_subphase = 7;
        sb_play_animal('c'); // Mèo kêu
    } else if (t >= 19000 && t < 19200 && s_demo_subphase == 7) {
        s_demo_subphase = 8;
        sb_play_animal('l'); // Sư tử gầm
    } else if (t >= 21500 && t < 21700 && s_demo_subphase == 8) {
        s_demo_subphase = 9;
        sb_play_animal('e'); // Voi gầm
    } else if (t >= 24000 && t < 24200 && s_demo_subphase == 9) {
        s_demo_subphase = 10;
        sb_play_animal('r'); // Gà trống gáy
    } else if (t >= 26500 && t < 26700 && s_demo_subphase == 10) {
        s_demo_subphase = 11;
        audio_cockpit_play_sfx(COCKPIT_SFX_SUCCESS); // Hoàn thành xuất sắc nhiệm vụ!
        led_effects_celebration();
    }

    if (t >= 4000 && t < 12000) {
        led_effects_police((t / 200) % 2);
    }
}

// ----------------------------------------------------------------------------
// MODE 9 DEMO: BÉ HỌC ĐÁNH VẦN & TẬP ĐẾM SỐ (Alphabet & Counting)
// ----------------------------------------------------------------------------
static const float s_abc_notes[] = {
    523.25f, 523.25f, 783.99f, 783.99f, 880.00f, 880.00f, 783.99f, // A B C D E F G
    698.46f, 698.46f, 659.25f, 659.25f, 587.33f, 587.33f, 523.25f, // H I J K L M N
    783.99f, 783.99f, 698.46f, 698.46f, 659.25f, 659.25f, 587.33f, // O P Q R S T U
    783.99f, 783.99f, 698.46f, 698.46f, 659.25f, 659.25f, 523.25f  // V W X Y Z
};
#define ABC_LEN (sizeof(s_abc_notes) / sizeof(s_abc_notes[0]))

static void tick_phonics_demo(uint32_t delta_ms) {
    (void)delta_ms;
    uint32_t t = s_demo_elapsed_ms;

    // Giai đoạn 1 (1.4s - 14s): Bài hát bảng chữ cái ABC vui tươi
    if (t < 14000) {
        if (s_demo_step_timer > delta_ms) {
            s_demo_step_timer -= delta_ms;
            return;
        }
        s_demo_step_timer = 450;
        float f = s_abc_notes[s_demo_note_idx % ABC_LEN];
        s_demo_note_idx++;
        audio_play_piano_note_dur(f, TIMBRE_MUSIC_BOX, 0, 400);
        led_effects_color_burst(100, 220, 255);
        return;
    }

    // Giai đoạn 2 (14s - 26s): Đếm số vui nhộn 1 -> 5 kèm nhịp trống
    if (t >= 14500 && t < 14700 && s_demo_subphase == 0) {
        s_demo_subphase = 1;
        sb_play_drum(DRUM_KICK);
        audio_play_counting_number(1); // "Một"
        led_effects_count(1, 255, 60, 60);
    } else if (t >= 16800 && t < 17000 && s_demo_subphase == 1) {
        s_demo_subphase = 2;
        sb_play_drum(DRUM_KICK);
        audio_play_counting_number(2); // "Hai"
        led_effects_count(2, 255, 160, 0);
    } else if (t >= 19100 && t < 19300 && s_demo_subphase == 2) {
        s_demo_subphase = 3;
        sb_play_drum(DRUM_KICK);
        audio_play_counting_number(3); // "Ba"
        led_effects_count(3, 255, 230, 0);
    } else if (t >= 21400 && t < 21600 && s_demo_subphase == 3) {
        s_demo_subphase = 4;
        sb_play_drum(DRUM_KICK);
        audio_play_counting_number(4); // "Bốn"
        led_effects_count(4, 60, 255, 80);
    } else if (t >= 23700 && t < 23900 && s_demo_subphase == 4) {
        s_demo_subphase = 5;
        sb_play_drum(DRUM_CYMBAL);
        audio_play_counting_number(5); // "Năm"
        led_effects_count(5, 0, 220, 255);
    } else if (t >= 26200 && t < 26400 && s_demo_subphase == 5) {
        s_demo_subphase = 6;
        audio_play_praise(); // "Bé giỏi quá!"
        led_effects_celebration();
    }
}

// ----------------------------------------------------------------------------
// MAIN DEMO TICK DISPATCHER (Gọi mỗi 50ms từ FreeRTOS Background Task)
// ----------------------------------------------------------------------------
void mode_demo_tick(uint32_t delta_ms) {
    if (!s_demo_active) return;

    s_demo_elapsed_ms += delta_ms;

    // Hết 30 giây: Tự động hoàn thành bản demo
    if (s_demo_elapsed_ms >= 30000) {
        mode_demo_stop();
        return;
    }

    // Chờ 1.2 giây đầu tiên để câu đọc tên chế độ "Chế độ X: ..." phát xong tròn vành rõ chữ
    if (s_demo_elapsed_ms < 1200) {
        return;
    }

    switch (s_demo_mode) {
        case 0: tick_piano_demo(delta_ms); break;      // Mode 1: Piano
        case 1: audio_mozart_autoplay_tick(); break;   // Mode 2: Baby Mozart
        case 2: tick_speed_demo(delta_ms); break;      // Mode 3: Speed Melody
        case 3: tick_drum_demo(delta_ms); break;       // Mode 4: Drum & DJ
        case 4: tick_typewriter_demo(delta_ms); break; // Mode 5: Typewriter
        case 5: tick_cartoon_demo(delta_ms); break;    // Mode 6: Cartoon SFX
        case 6: tick_agent_demo(delta_ms); break;      // Mode 7: Secret Agent & Robot
        case 7: tick_cockpit_demo(delta_ms); break;    // Mode 8: Cockpit & Animals
        case 8: tick_phonics_demo(delta_ms); break;    // Mode 9: Phonics & Counting
        default:
            // Mode 10 (USB MP3 Player): Không cần demo vì tự phát nhạc USB
            break;
    }
}
