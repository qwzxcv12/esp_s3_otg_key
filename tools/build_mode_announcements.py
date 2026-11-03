import os
import sys
import numpy as np
import soundfile as sf
import scipy.signal as sps
from piper.voice import PiperVoice

sys.stdout.reconfigure(encoding='utf-8')

ONNX_PATH = "tools/_cache/vi_VN-vais1000-medium.onnx"
OUT_DIR = "audio_assets/common"
TARGET_SR = 24000

os.makedirs(OUT_DIR, exist_ok=True)

modes_text = [
    ("mode_1", "Chế độ 1: Đàn Piano diệu kỳ!"),
    ("mode_2", "Chế độ 2: Nghệ sĩ thiên tài Baby Mozart!"),
    ("mode_3", "Chế độ 3: Nhạc chạy theo tốc độ tay!"),
    ("mode_4", "Chế độ 4: Dàn trống Beat Maker và DJ!"),
    ("mode_5", "Chế độ 5: Máy gõ chữ cổ điển!"),
    ("mode_6", "Chế độ 6: Hộp âm thanh game và hoạt hình!"),
    ("mode_7", "Chế độ 7: Điệp viên nhí và rô-bốt!"),
    ("mode_8", "Chế độ 8: Buồng lái xe cứu hỏa và vườn thú!"),
    ("mode_9", "Chế độ 9: Bé học đánh vần chữ cái và tập đếm số!"),
]

print("Loading Piper Voice...")
v = PiperVoice.load(ONNX_PATH)
piper_sr = v.config.sample_rate

# Optional short intro chime (120ms ting)
chime_raw, chime_sr = sf.read("audio_assets/common/chime.wav")
if chime_sr != TARGET_SR:
    chime_raw = sps.resample_poly(chime_raw, TARGET_SR, chime_sr)
chime_short = chime_raw[:int(TARGET_SR * 0.18)]
fo = int(TARGET_SR * 0.06)
chime_short[-fo:] *= np.linspace(1.0, 0.0, fo)
chime_short *= 0.45 # gentle chime level

for name, text in modes_text:
    dest_path = f"{OUT_DIR}/{name}.wav"
    print(f"Synthesizing '{text}' -> {dest_path}")
    chunks = list(v.synthesize(text))
    raw = b''.join(c.audio_int16_bytes for c in chunks)
    arr = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    
    # Resample to 24000
    if piper_sr != TARGET_SR:
        arr = sps.resample_poly(arr, TARGET_SR, piper_sr)
        
    # Trim leading/trailing silence
    thresh = 0.015
    indices = np.where(np.abs(arr) > thresh)[0]
    if len(indices) > 0:
        start = max(0, indices[0] - int(TARGET_SR * 0.03))
        end = min(len(arr), indices[-1] + int(TARGET_SR * 0.06))
        arr = arr[start:end]
        
    # Normalize speech part
    pk = np.max(np.abs(arr))
    if pk > 0.01:
        arr = arr * (0.85 / pk)
        
    # Combine: [chime_short (180ms)] + [silence 40ms] + [speech]
    pause = np.zeros(int(TARGET_SR * 0.04), dtype=np.float32)
    combined = np.concatenate([chime_short, pause, arr])
    
    pcm16 = (np.clip(combined, -1.0, 1.0) * 32767.0).astype(np.int16)
    sf.write(dest_path, pcm16, TARGET_SR, subtype="PCM_16")
    dur = len(pcm16) / TARGET_SR
    print(f" -> {dest_path} ({dur:.2f}s, {len(pcm16)*2/1024:.1f} KB)")
