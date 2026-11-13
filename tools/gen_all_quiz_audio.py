import os
import subprocess
import soundfile as sf
import scipy.signal as sps
import numpy as np

TARGET_SR = 16000

# 1. Synthesize spell clips with piper
spell_texts = {
    'spell_intro': 'Đố bé tìm thấy',
    'spell_outro': 'ở đâu nào?',
    'hint_wrong_spell': 'Chưa đúng rồi, bé tìm lại nhé!'
}

model_path = 'tools/_cache/vi_VN-vais1000-medium.onnx'

for name, text in spell_texts.items():
    raw_wav = f'tools/_cache/{name}_raw.wav'
    out_wav = f'tools/_cache/{name}.wav'
    print(f"Synthesizing {name}: {text}")
    
    cmd = ['piper', '-m', model_path, '-f', raw_wav]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, text=True, encoding='utf-8')
    p.communicate(input=text)
    
    data, sr = sf.read(raw_wav)
    if data.ndim > 1:
        data = np.mean(data, axis=1)
    if sr != TARGET_SR:
        data = sps.resample_poly(data, TARGET_SR, sr)
        
    thresh = 0.015
    act = np.where(np.abs(data) > thresh)[0]
    if len(act) > 0:
        s = max(0, act[0] - int(TARGET_SR * 0.01))
        e = min(len(data), act[-1] + int(TARGET_SR * 0.02))
        data = data[s:e]
        
    fo = min(len(data), int(TARGET_SR * 0.015))
    if fo > 0:
        data[-fo:] *= np.linspace(1.0, 0.0, fo)
    fi = min(len(data), int(TARGET_SR * 0.005))
    if fi > 0:
        data[:fi] *= np.linspace(0.0, 1.0, fi)
        
    pk = np.max(np.abs(data))
    if pk > 0.001:
        data = data * (0.88 / pk)
        
    pcm16 = (np.clip(data, -1.0, 1.0) * 32767.0).astype(np.int16)
    sf.write(out_wav, pcm16, TARGET_SR, subtype='PCM_16')
    print(f" -> {name}.wav: {len(pcm16)/TARGET_SR:.2f}s, {len(pcm16)*2/1024:.1f} KB")

# 2. Build combined C code
words = [
    'num_0', 'num_1', 'num_2', 'num_3', 'num_4', 'num_5', 'num_6',
    'num_7', 'num_8', 'num_9', 'num_10', 'op_plus', 'op_minus', 'ask_equals',
    'ans_bang', 'praise_correct', 'hint_wrong',
    'spell_intro', 'spell_outro', 'hint_wrong_spell'
]

enum_names = [
    'MATH_SND_NUM_0 = 0',
    'MATH_SND_NUM_1',
    'MATH_SND_NUM_2',
    'MATH_SND_NUM_3',
    'MATH_SND_NUM_4',
    'MATH_SND_NUM_5',
    'MATH_SND_NUM_6',
    'MATH_SND_NUM_7',
    'MATH_SND_NUM_8',
    'MATH_SND_NUM_9',
    'MATH_SND_NUM_10',
    'MATH_SND_PLUS',
    'MATH_SND_MINUS',
    'MATH_SND_ASK_EQUALS',
    'MATH_SND_ANS_BANG',
    'MATH_SND_PRAISE_CORRECT',
    'MATH_SND_HINT_WRONG',
    'MATH_SND_SPELL_INTRO',
    'MATH_SND_SPELL_OUTRO',
    'MATH_SND_SPELL_HINT_WRONG',
    'MATH_SND_COUNT'
]

enum_body = ',\n    '.join(enum_names)
h_code = f"""#ifndef MATH_AUDIO_DATA_H
#define MATH_AUDIO_DATA_H

#include <stdint.h>
#include <stdbool.h>

#define MATH_AUDIO_SAMPLE_RATE 16000

typedef enum {{
    {enum_body}
}} math_snd_id_t;

const int16_t *math_audio_get_pcm(math_snd_id_t id, uint32_t *num_samples);
bool math_audio_play(math_snd_id_t id, uint32_t *duration_ms);

#endif // MATH_AUDIO_DATA_H
"""

with open('main/modes/math_audio_data.h', 'w', encoding='utf-8') as f:
    f.write(h_code)

c_code = """#include "math_audio_data.h"
#include "audio_synth.h"
#include <stddef.h>

"""

total_bytes = 0
for w in words:
    data, sr = sf.read(f'tools/_cache/{w}.wav')
    if data.dtype != np.int16:
        data = (data * 32767.0).astype(np.int16)
    total_bytes += len(data) * 2
    c_code += f'static const int16_t s_pcm_{w}[{len(data)}] = {{\n'
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        c_code += '    ' + ', '.join(str(int(v)) for v in chunk) + ',\n'
    c_code += '};\n\n'

asset_entries = [f'    {{ s_pcm_{w}, sizeof(s_pcm_{w})/sizeof(int16_t) }},' for w in words]

entries_str = '\n'.join(asset_entries)

c_code += f"""static const struct {{
    const int16_t *pcm;
    uint32_t len;
}} s_math_assets[MATH_SND_COUNT] = {{
{entries_str}
}};

const int16_t *math_audio_get_pcm(math_snd_id_t id, uint32_t *num_samples) {{
    if ((int)id < 0 || id >= MATH_SND_COUNT) return NULL;
    if (num_samples) *num_samples = s_math_assets[id].len;
    return s_math_assets[id].pcm;
}}

bool math_audio_play(math_snd_id_t id, uint32_t *duration_ms) {{
    if ((int)id < 0 || id >= MATH_SND_COUNT) return false;
    pcm_asset_t asset = {{
        .pcm = (int16_t *)s_math_assets[id].pcm,
        .num_samples = s_math_assets[id].len,
        .sample_rate = MATH_AUDIO_SAMPLE_RATE
    }};
    if (duration_ms) {{
        *duration_ms = (s_math_assets[id].len * 1000u) / MATH_AUDIO_SAMPLE_RATE;
    }}
    return audio_sample_play(&asset, 1.45f, 0, false, 1 /* CHOKE_SPEECH */, 0, 0, 0) >= 0;
}}
"""

with open('main/modes/math_audio_data.c', 'w', encoding='utf-8') as f:
    f.write(c_code)

print(f"Generated math_audio_data.h and math_audio_data.c successfully! Total size: {total_bytes/1024:.1f} KB")
