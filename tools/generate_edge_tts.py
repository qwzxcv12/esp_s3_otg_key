import os
import sys
import asyncio
import numpy as np
import soundfile as sf
import edge_tts

sys.stdout.reconfigure(encoding='utf-8')

VOICE = "vi-VN-HoaiMyNeural"
OUT_DIR = "audio_assets/mode9"
TARGET_SR = 24000

os.makedirs(OUT_DIR, exist_ok=True)

letter_map = {
    'a': 'Chữ A',
    'b': 'Chữ B',
    'c': 'Chữ C',
    'd': 'Chữ D',
    'e': 'Chữ E',
    'f': 'Chữ F',
    'g': 'Chữ G',
    'h': 'Chữ H',
    'i': 'Chữ I',
    'j': 'Chữ J',
    'k': 'Chữ K',
    'l': 'Chữ L',
    'm': 'Chữ M',
    'n': 'Chữ N',
    'o': 'Chữ O',
    'p': 'Chữ P',
    'q': 'Chữ Q',
    'r': 'Chữ R',
    's': 'Chữ S',
    't': 'Chữ T',
    'u': 'Chữ U',
    'v': 'Chữ V',
    'w': 'Chữ W',
    'x': 'Chữ X',
    'y': 'Chữ Y',
    'z': 'Chữ Z'
}

number_map = {
    "num_1": "Số 1",
    "num_2": "Số 2",
    "num_3": "Số 3",
    "num_4": "Số 4",
    "num_5": "Số 5",
    "num_6": "Số 6",
    "num_7": "Số 7",
    "num_8": "Số 8",
    "num_9": "Số 9",
    "num_10": "Số 10"
}

praise_text = "Hoan hô! Bé giỏi quá!"

async def synthesize_one(text, dest_wav):
    temp_mp3 = dest_wav.replace(".wav", "_temp.mp3")
    for attempt in range(3):
        try:
            communicate = edge_tts.Communicate(text, VOICE)
            await communicate.save(temp_mp3)
            break
        except Exception as e:
            if attempt == 2:
                raise e
            await asyncio.sleep(1.0)
    
    # Read synthesized audio
    data, sr = sf.read(temp_mp3)
    if os.path.exists(temp_mp3):
        os.remove(temp_mp3)
        
    if data.ndim > 1:
        data = np.mean(data, axis=1)
        
    # Trim leading silence (< -35 dB)
    thresh = 0.015
    start_idx = 0
    for idx, val in enumerate(data):
        if abs(val) > thresh:
            start_idx = max(0, idx - int(sr * 0.010))  # 10ms padding
            break
    data = data[start_idx:]

    # Trim trailing silence (< -35 dB)
    end_idx = len(data)
    for idx in range(len(data) - 1, -1, -1):
        if abs(data[idx]) > thresh:
            end_idx = min(len(data), idx + int(sr * 0.020)) # 20ms padding
            break
    data = data[:end_idx]

    # Peak normalize to 0.95
    pk = np.max(np.abs(data))
    if pk > 0.001:
        data = data * (0.95 / pk)

    # 5ms fade-in, 15ms fade-out
    fi = min(len(data), int(sr * 0.005))
    if fi > 0:
        data[:fi] *= np.linspace(0, 1, fi)
    fo = min(len(data), int(sr * 0.015))
    if fo > 0:
        data[-fo:] *= np.linspace(1, 0, fo)

    pcm16 = (np.clip(data, -1.0, 1.0) * 32767.0).astype(np.int16)
    sf.write(dest_wav, pcm16, sr, subtype="PCM_16")
    dur = len(pcm16) / sr
    print(f" -> {os.path.basename(dest_wav)}: '{text}' ({dur:.2f}s, {len(pcm16)*2/1024:.1f} KB)")

async def main():
    print(f"============================================================")
    print(f"  TẠO ÂM THANH TIẾNG VIỆT MICROSOFT EDGE TTS ({VOICE})")
    print(f"============================================================")

    # 1. Letters
    print("\n>>> Đang tạo 26 chữ cái tiếng Việt...")
    for char, text in letter_map.items():
        dest = os.path.join(OUT_DIR, f"letter_{char}.wav")
        await synthesize_one(text, dest)
        await asyncio.sleep(0.15)

    # 2. Numbers
    print("\n>>> Đang tạo 10 số đếm tiếng Việt...")
    for name, text in number_map.items():
        dest = os.path.join(OUT_DIR, f"{name}.wav")
        await synthesize_one(text, dest)
        await asyncio.sleep(0.15)

    # 3. Praise
    print("\n>>> Đang tạo câu khen ngợi...")
    dest = os.path.join(OUT_DIR, "praise.wav")
    await synthesize_one(praise_text, dest)

    print("\n>>> HOÀN TẤT TẠO TOÀN BỘ KHO ÂM THANH MICROSOFT EDGE TTS!")

if __name__ == '__main__':
    asyncio.run(main())
