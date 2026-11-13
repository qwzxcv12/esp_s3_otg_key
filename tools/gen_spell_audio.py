import asyncio
import edge_tts
import soundfile as sf
import scipy.signal as sps
import numpy as np
import os

VOICE = "vi-VN-HoaiMyNeural"
TARGET_SR = 16000

items = {
    'spell_intro': 'Đố bé tìm thấy.',
    'spell_outro': 'ở đâu nào?',
    'spell_hint_wrong': 'Chưa đúng rồi, bé tìm lại nhé!'
}

async def generate():
    os.makedirs('tools/_cache', exist_ok=True)
    for k, text in items.items():
        mp3 = f'tools/_cache/{k}.mp3'
        wav = f'tools/_cache/{k}.wav'
        print(f"Synthesizing {k}: {text}")
        comm = edge_tts.Communicate(text, VOICE, rate="+10%")
        await comm.save(mp3)
        
        data, sr = sf.read(mp3)
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
            data = data * (0.85 / pk)
            
        pcm16 = (np.clip(data, -1.0, 1.0) * 32767.0).astype(np.int16)
        sf.write(wav, pcm16, TARGET_SR, subtype='PCM_16')
        print(f" -> {k}.wav: {len(pcm16)/TARGET_SR:.2f}s, {len(pcm16)*2/1024:.1f} KB")

asyncio.run(generate())
