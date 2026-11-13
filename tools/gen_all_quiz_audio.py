import os
import soundfile as sf
import numpy as np

TARGET_SR = 16000

def pcm16_to_ulaw(pcm_val):
    BIAS = 0x84
    CLIP = 32635
    sign = 0x80 if pcm_val < 0 else 0
    val = abs(int(pcm_val))
    if val > CLIP:
        val = CLIP
    val += BIAS
    exponent = 7
    for exp in range(7):
        if val < (1 << (exp + 8)):
            exponent = exp
            break
    mantissa = (val >> (exponent + 3)) & 0x0F
    return (~(sign | (exponent << 4) | mantissa)) & 0xFF

def ulaw_to_pcm16(u):
    u = ~u & 0xFF
    sign = u & 0x80
    exponent = (u >> 4) & 0x07
    mantissa = u & 0x0F
    sample = ((mantissa << 3) + 0x84) << exponent
    sample -= 0x84
    return -sample if sign != 0 else sample

# Base sounds
base_files = {
    'MATH_SND_NUM_0': 'tools/_cache/num_0.wav',
    'MATH_SND_NUM_1': 'tools/_cache/num_1.wav',
    'MATH_SND_NUM_2': 'tools/_cache/num_2.wav',
    'MATH_SND_NUM_3': 'tools/_cache/num_3.wav',
    'MATH_SND_NUM_4': 'tools/_cache/num_4.wav',
    'MATH_SND_NUM_5': 'tools/_cache/num_5.wav',
    'MATH_SND_NUM_6': 'tools/_cache/num_6.wav',
    'MATH_SND_NUM_7': 'tools/_cache/num_7.wav',
    'MATH_SND_NUM_8': 'tools/_cache/num_8.wav',
    'MATH_SND_NUM_9': 'tools/_cache/num_9.wav',
    'MATH_SND_NUM_10': 'tools/_cache/num_10.wav',
    'MATH_SND_PLUS': 'tools/_cache/op_plus.wav',
    'MATH_SND_MINUS': 'tools/_cache/op_minus.wav',
    'MATH_SND_ASK_EQUALS': 'tools/_cache/ask_equals.wav',
    'MATH_SND_ANS_BANG': 'tools/_cache/ans_bang.wav',
    'MATH_SND_PRAISE_CORRECT': 'tools/_cache/praise_correct.wav',
    'MATH_SND_HINT_WRONG': 'tools/_cache/hint_wrong.wav',
    'MATH_SND_SPELL_INTRO': 'tools/_cache/spell_intro.wav',
    'MATH_SND_SPELL_OUTRO': 'tools/_cache/spell_outro.wav',
    'MATH_SND_SPELL_HINT_WRONG': 'tools/_cache/hint_wrong_spell.wav'
}

# 26 Word files
word_files = {}
for ch in range(ord('A'), ord('Z') + 1):
    c = chr(ch)
    enum_name = f'MATH_SND_WORD_{c}'
    file_path = f'tools/_cache/words_16k/word_{c.lower()}.wav'
    word_files[enum_name] = file_path

all_clips = {}
all_clips.update(base_files)
all_clips.update(word_files)

total_ulaw_bytes = 0
clip_arrays = {}

for enum_id, wav_path in all_clips.items():
    data, sr = sf.read(wav_path)
    if data.ndim > 1:
        data = np.mean(data, axis=1)
    if data.dtype != np.int16:
        pcm = (np.clip(data, -1.0, 1.0) * 32767.0).astype(np.int16)
    else:
        pcm = data
    ulaw_bytes = bytes([pcm16_to_ulaw(v) for v in pcm])
    clip_arrays[enum_id] = ulaw_bytes
    total_ulaw_bytes += len(ulaw_bytes)
    print(f'{enum_id}: {len(ulaw_bytes)} samples ({len(ulaw_bytes)/1024:.1f} KB)')

print(f"Total compressed audio size across all {len(all_clips)} clips: {total_ulaw_bytes/1024:.1f} KB")

# Generate Header file
enum_list = list(all_clips.keys())
enum_list_str = ',\n    '.join(enum_list) + ',\n    MATH_SND_COUNT'

h_content = f"""#ifndef MATH_AUDIO_DATA_H
#define MATH_AUDIO_DATA_H

#include <stdint.h>
#include <stdbool.h>

#define MATH_AUDIO_SAMPLE_RATE 16000

typedef enum {{
    {enum_list_str}
}} math_snd_id_t;

bool math_audio_play(math_snd_id_t id, uint32_t *duration_ms);
bool math_audio_play_word(char letter, uint32_t *duration_ms);
const char *math_audio_get_word_text(char letter);

#endif // MATH_AUDIO_DATA_H
"""

with open('main/modes/math_audio_data.h', 'w', encoding='utf-8') as f:
    f.write(h_content)

# Generate C file
# 1. Precalculate 256-entry lookup table for C
lut = [ulaw_to_pcm16(b) for b in range(256)]
lut_rows = []
for i in range(0, 256, 8):
    chunk = lut[i:i+8]
    lut_rows.append('    ' + ', '.join(f'{x:6d}' for x in chunk))
lut_str = ',\n'.join(lut_rows)

c_content = f"""#include "math_audio_data.h"
#include "audio_synth.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stddef.h>
#include <stdlib.h>

static const char *TAG = "math_audio";

// Standard ITU-T G.711 mu-law to 16-bit PCM lookup table (256 entries)
static const int16_t s_mulaw_to_pcm16[256] = {{
{lut_str}
}};

// Double-buffering in PSRAM to prevent race condition during voice release fade-out
#define DECODE_BUF_SAMPLES 36000
static int16_t *s_pcm_buf[2] = {{NULL, NULL}};
static uint8_t s_buf_idx = 0;

static bool ensure_decode_buffers(void) {{
    if (!s_pcm_buf[0]) {{
        s_pcm_buf[0] = (int16_t *)heap_caps_malloc(DECODE_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pcm_buf[0]) {{
            s_pcm_buf[0] = (int16_t *)malloc(DECODE_BUF_SAMPLES * sizeof(int16_t));
        }}
    }}
    if (!s_pcm_buf[1]) {{
        s_pcm_buf[1] = (int16_t *)heap_caps_malloc(DECODE_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pcm_buf[1]) {{
            s_pcm_buf[1] = (int16_t *)malloc(DECODE_BUF_SAMPLES * sizeof(int16_t));
        }}
    }}
    return (s_pcm_buf[0] != NULL && s_pcm_buf[1] != NULL);
}}

"""

for enum_id, raw_bytes in clip_arrays.items():
    var_name = enum_id.lower()
    c_content += f'static const uint8_t s_ulaw_{var_name}[{len(raw_bytes)}] = {{\n'
    for i in range(0, len(raw_bytes), 20):
        chunk = raw_bytes[i:i+20]
        c_content += '    ' + ', '.join(f'0x{b:02X}' for b in chunk) + ',\n'
    c_content += '};\n\n'

asset_entries = [f'    {{ s_ulaw_{enum_id.lower()}, sizeof(s_ulaw_{enum_id.lower()}) }},' for enum_id in all_clips.keys()]
entries_str = '\n'.join(asset_entries)

c_content += f"""static const struct {{
    const uint8_t *ulaw;
    uint32_t len;
}} s_math_assets[MATH_SND_COUNT] = {{
{entries_str}
}};

static const char *s_word_texts[26] = {{
    "Con cá",    "Con bò",    "Con cò",    "Con dê",
    "Em bé",     "Con ếch",   "Con gà",    "Con hổ",
    "Con khỉ",   "Báo đốm",   "Cái kéo",   "Con lợn",
    "Con mèo",   "Con nai",   "Con ong",   "Đèn pin",
    "Con quạ",   "Con rùa",   "Con sóc",   "Con thỏ",
    "Quả đu đủ", "Con voi",   "Chó sói",   "Xe đạp",
    "Chim yến",  "Ngựa vằn"
}};

const char *math_audio_get_word_text(char letter) {{
    if (letter >= 'a' && letter <= 'z') letter -= 32;
    if (letter < 'A' || letter > 'Z') return "";
    return s_word_texts[letter - 'A'];
}}

bool math_audio_play(math_snd_id_t id, uint32_t *duration_ms) {{
    if ((int)id < 0 || id >= MATH_SND_COUNT) return false;
    if (!ensure_decode_buffers()) {{
        ESP_LOGE(TAG, "Failed to allocate decode buffer for audio");
        return false;
    }}

    uint32_t samples = s_math_assets[id].len;
    if (samples > DECODE_BUF_SAMPLES) samples = DECODE_BUF_SAMPLES;

    int16_t *dest = s_pcm_buf[s_buf_idx];
    const uint8_t *src = s_math_assets[id].ulaw;

    for (uint32_t i = 0; i < samples; i++) {{
        dest[i] = s_mulaw_to_pcm16[src[i]];
    }}

    if (duration_ms) {{
        *duration_ms = (samples * 1000u) / MATH_AUDIO_SAMPLE_RATE;
    }}

    pcm_asset_t asset = {{
        .pcm = dest,
        .num_samples = samples,
        .sample_rate = MATH_AUDIO_SAMPLE_RATE
    }};

    s_buf_idx = 1 - s_buf_idx;
    return audio_sample_play(&asset, 1.45f, 0, false, 1 /* CHOKE_SPEECH */, 0, 0, 0) >= 0;
}}

bool math_audio_play_word(char letter, uint32_t *duration_ms) {{
    if (letter >= 'a' && letter <= 'z') letter -= 32;
    if (letter < 'A' || letter > 'Z') return false;
    math_snd_id_t id = (math_snd_id_t)(MATH_SND_WORD_A + (letter - 'A'));
    return math_audio_play(id, duration_ms);
}}
"""

with open('main/modes/math_audio_data.c', 'w', encoding='utf-8') as f:
    f.write(c_content)

print(f"Generated math_audio_data.h and math_audio_data.c successfully!")
