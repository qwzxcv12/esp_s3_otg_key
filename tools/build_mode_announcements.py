import os
import sys
import numpy as np
import soundfile as sf
import scipy.signal as sps
from piper.voice import PiperVoice

sys.stdout.reconfigure(encoding='utf-8')

CACHE_DIR = "tools/_cache"
OUT_DIR = "audio_assets/common"
TARGET_SR = 24000

os.makedirs(OUT_DIR, exist_ok=True)

modes_text = [
    ("mode_1", "Chế độ 1: Piano."),
    ("mode_2", "Chế độ 2: Mozart."),
    ("mode_3", "Chế độ 3: Tốc độ."),
    ("mode_4", "Chế độ 4: Dàn trống."),
    ("mode_5", "Chế độ 5: Máy chữ."),
    ("mode_6", "Chế độ 6: Hoạt hình."),
    ("mode_7", "Chế độ 7: Điệp viên."),
    ("mode_8", "Chế độ 8: Buồng lái."),
    ("mode_9", "Chế độ 9: Học chữ."),
    ("mode_10", "Chế độ 10: Nhạc USB.")
]

onnx_path = os.path.join(CACHE_DIR, "vi_VN-vais1000-medium.onnx")
print(f"Loading Piper Voice from {onnx_path}...")
v = PiperVoice.load(onnx_path)
piper_sr = v.config.sample_rate

# Optional short intro chime (180ms ting)
chime_raw, chime_sr = sf.read("audio_assets/common/chime.wav")
if chime_sr != TARGET_SR:
    chime_raw = sps.resample_poly(chime_raw, TARGET_SR, chime_sr)
chime_short = chime_raw[:int(TARGET_SR * 0.18)]
fo = int(TARGET_SR * 0.06)
chime_short[-fo:] *= np.linspace(1.0, 0.0, fo)
chime_short *= 0.45 # gentle chime level

pause = np.zeros(int(TARGET_SR * 0.04), dtype=np.float32)

for name, text in modes_text:
    dest_path = f"{OUT_DIR}/{name}.wav"
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
    combined = np.concatenate([chime_short, pause, arr])
    
    pcm16 = (np.clip(combined, -1.0, 1.0) * 32767.0).astype(np.int16)
    sf.write(dest_path, pcm16, TARGET_SR, subtype="PCM_16")
    dur = len(pcm16) / TARGET_SR
    print(f" -> {dest_path} [{text}] ({dur:.2f}s, {len(pcm16)*2/1024:.1f} KB)")

print("\n>>> ALL SHORT MODE ANNOUNCEMENTS GENERATED SUCCESSFULLY!")
