import os
import io
import json
import urllib.request
import urllib.parse
import numpy as np
import soundfile as sf
import scipy.signal as sps
from piper.voice import PiperVoice

CACHE_DIR = "tools/_cache"
OUT_DIR = "audio_assets"
TARGET_SR = 24000

os.makedirs(CACHE_DIR, exist_ok=True)

def download_cache(url, filename):
    filepath = os.path.join(CACHE_DIR, filename)
    if os.path.exists(filepath) and os.path.getsize(filepath) > 0:
        return filepath
    print(f"Downloading {url} ...")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (ESP32-Audio-Builder)"})
    with urllib.request.urlopen(req) as resp, open(filepath, "wb") as out:
        out.write(resp.read())
    return filepath

def export_wav(dest_path, audio_arr, sr, max_dur=None, semitone_shift=0.0, overwrite=False, peak_gain=0.84):
    if not overwrite and os.path.exists(dest_path) and os.path.getsize(dest_path) > 100:
        return
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    if audio_arr.ndim > 1:
        audio_arr = np.mean(audio_arr, axis=1) # Mono
    
    # Pitch shift if needed via resampling
    if semitone_shift != 0.0:
        ratio = 2.0 ** (-semitone_shift / 12.0)
        up = int(np.round(1000 * ratio))
        down = 1000
        audio_arr = sps.resample_poly(audio_arr, up, down)

    # Resample to 24000 Hz
    if sr != TARGET_SR:
        audio_arr = sps.resample_poly(audio_arr, TARGET_SR, sr)
    
    # Trim leading silence gracefully without chopping transient
    thresh = 0.005
    start_idx = 0
    for idx, val in enumerate(audio_arr[:TARGET_SR]):
        if abs(val) > thresh:
            start_idx = max(0, idx - int(TARGET_SR * 0.015)) # 15ms headroom
            break
    audio_arr = audio_arr[start_idx:]
    
    # Trim duration if requested
    if max_dur:
        max_samples = int(TARGET_SR * max_dur)
        if len(audio_arr) > max_samples:
            audio_arr = audio_arr[:max_samples]
            # Smooth musical fade-out of 180ms at end
            fo = min(len(audio_arr), int(TARGET_SR * 0.180))
            if fo > 0:
                audio_arr[-fo:] *= np.linspace(1.0, 0.0, fo)
            
    # Apply soft 5ms fade-in
    fade_in = min(len(audio_arr), int(TARGET_SR * 0.005))
    if fade_in > 0:
        audio_arr[:fade_in] *= np.linspace(0, 1, fade_in)
        
    # Remove DC offset
    audio_arr -= np.mean(audio_arr)
    
    # Normalize peak to peak_gain
    pk = np.max(np.abs(audio_arr))
    if pk > 0.001:
        audio_arr = audio_arr * (peak_gain / pk)
        
    pcm16 = (np.clip(audio_arr, -1.0, 1.0) * 32767.0).astype(np.int16)
    sf.write(dest_path, pcm16, TARGET_SR, subtype="PCM_16")
    dur = len(pcm16) / TARGET_SR
    print(f" -> {dest_path} ({len(pcm16)} samples, {dur:.2f}s, {len(pcm16)*2/1024:.1f} KB)")

# ============================================================================
# 1. MODE 9: VIETNAMESE TTS (PHONICS, NUMBERS, PRAISE)
# ============================================================================
def build_mode9():
    print("\n=== Building Mode 9: Phonics & Numbers (Piper Vietnamese TTS) ===")
    onnx_path = os.path.join(CACHE_DIR, "vi_VN-vais1000-medium.onnx")
    v = PiperVoice.load(onnx_path)
    
    # Numbers
    num_texts = {
        "num_1": "Một", "num_2": "Hai", "num_3": "Ba", "num_4": "Bốn", "num_5": "Năm",
        "num_6": "Sáu", "num_7": "Bảy", "num_8": "Tám", "num_9": "Chín", "num_10": "Mười"
    }
    for name, text in num_texts.items():
        chunks = list(v.synthesize(text))
        raw = b''.join(c.audio_int16_bytes for c in chunks)
        arr = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
        export_wav(f"{OUT_DIR}/mode9/{name}.wav", arr, v.config.sample_rate)

    # Praise
    chunks = list(v.synthesize("Hoan hô, bé giỏi quá!"))
    raw = b''.join(c.audio_int16_bytes for c in chunks)
    arr = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    export_wav(f"{OUT_DIR}/mode9/praise.wav", arr, v.config.sample_rate)

    # Vietnamese Alphabet Letters (Phonics)
    letter_texts = {
        'a': 'A', 'b': 'Bê', 'c': 'Xê', 'd': 'Dê', 'e': 'E', 'f': 'Ép',
        'g': 'Giê', 'h': 'Hát', 'i': 'I', 'j': 'Di', 'k': 'Ca', 'l': 'E-lờ',
        'm': 'Em-mờ', 'n': 'En-nờ', 'o': 'O', 'p': 'Pê', 'q': 'Quy', 'r': 'E-rờ',
        's': 'Ét', 't': 'Tê', 'u': 'U', 'v': 'Vê', 'w': 'Vê kép', 'x': 'Ích',
        'y': 'I dài', 'z': 'Dét'
    }
    for char, text in letter_texts.items():
        chunks = list(v.synthesize(text))
        raw = b''.join(c.audio_int16_bytes for c in chunks)
        arr = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
        export_wav(f"{OUT_DIR}/mode9/letter_{char}.wav", arr, v.config.sample_rate)

# ============================================================================
# 2. MODE 1: KAWAI GRAND PIANO & HAND CHIMES / GLOCKENSPIEL (VCSL CC0)
# ============================================================================
def build_mode1():
    print("\n=== Building Mode 1: Kawai Grand Piano & Music Box Bells (VCSL CC0) ===")
    VCSL_BASE = "https://raw.githubusercontent.com/sgossner/VCSL/master/"
    
    # 10 Root Notes: C2(36), F#2(42), C3(48), F#3(54), C4(60), F#4(66), C5(72), F#5(78), C6(84), C7(96)
    # Direct authentic recordings from Kawai Grand Piano (Player perspective) - 0.0 semitone shift!
    piano_roots = [
        ("piano_c2",  "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C1_v3_rr1_Player.wav"),
        ("piano_fs2", "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#1_v3_rr1_Player.wav"),
        ("piano_c3",  "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C2_v3_rr1_Player.wav"),
        ("piano_fs3", "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#2_v3_rr1_Player.wav"),
        ("piano_c4",  "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C3_v3_rr1_Player.wav"),
        ("piano_fs4", "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#3_v3_rr1_Player.wav"),
        ("piano_c5",  "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C4_v3_rr1_Player.wav"),
        ("piano_fs5", "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#4_v3_rr1_Player.wav"),
        ("piano_c6",  "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C5_v3_rr1_Player.wav"),
        ("piano_c7",  "Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C6_v3_rr1_Player.wav"),
    ]
    
    for dest_name, rel_path in piano_roots:
        url = VCSL_BASE + urllib.parse.quote(rel_path)
        cached_file = download_cache(url, os.path.basename(rel_path))
        data, sr = sf.read(cached_file)
        export_wav(f"{OUT_DIR}/mode1/{dest_name}.wav", data, sr, max_dur=2.6, semitone_shift=0.0, overwrite=True)

    # 4 Music Box / Bell Root Notes: C4(60), C5(72), C6(84), C7(96)
    # Direct authentic recordings verified by FFT pitch analysis - 0.0 semitone shift!
    bell_samples = [
        ("bell_c4", "Idiophones/Struck Idiophones/Hand Chimes/sus_C3_r01_main.wav", 0.45),       # Hand Chimes C4 (261.6 Hz, MIDI 60)
        ("bell_c5", "Idiophones/Struck Idiophones/Hand Chimes/sus_C4_r01_main.wav", 0.45),       # Hand Chimes C5 (523.2 Hz, MIDI 72)
        ("bell_c6", "Idiophones/Struck Idiophones/Glockenspiel/glock_loud_C5_01.wav", 0.84),     # Glockenspiel C6 (1046.5 Hz, MIDI 84)
        ("bell_c7", "Idiophones/Struck Idiophones/Glockenspiel/glock_loud_C6_01.wav", 0.84),     # Glockenspiel C7 (2093.0 Hz, MIDI 96)
    ]
    for dest_name, rel_path, pk_gain in bell_samples:
        url = VCSL_BASE + urllib.parse.quote(rel_path)
        cached_file = download_cache(url, os.path.basename(rel_path))
        data, sr = sf.read(cached_file)
        export_wav(f"{OUT_DIR}/mode1/{dest_name}.wav", data, sr, max_dur=2.0, semitone_shift=0.0, overwrite=True, peak_gain=pk_gain)

# ============================================================================
# 3. COMMON & MODE 4: DRUMS & PERCUSSION (VCSL CC0)
# ============================================================================
def build_mode4_and_common():
    print("\n=== Building Mode 4: Drum Kit & Common (VCSL CC0) ===")
    VCSL_BASE = "https://raw.githubusercontent.com/sgossner/VCSL/master/"
    
    percussions = [
        ("common/chime", "Idiophones/Struck Idiophones/Triangles/Legacy/1/triangle1_hit_mf_pt2.wav", 0.8),
        ("common/coin",  "Idiophones/Struck Idiophones/Glockenspiel/glock_loud_C7_01.wav", 0.4),
        ("mode4/kick",   "Membranophones/Struck Membranophones/Bass Drum 1/BDrumNew_hit_v2_rr1_Sum.wav", 0.4),
        ("mode4/snare",  "Membranophones/Struck Membranophones/Legacy Snares/OldSnare/snare_f1.wav", 0.35),
        ("mode4/hihat",  "Idiophones/Struck Idiophones/Hi-Hat Cymbal/HiHat_Close_rr1_Mid.wav", 0.20),
        ("mode4/cymbal", "Idiophones/Struck Idiophones/Clash Cymbals 1/cymbal_crash1_mf1.wav", 1.0),
        ("mode4/tom",    "Membranophones/Struck Membranophones/Legacy Toms/tenor_higher/tenorH_f_rr1.wav", 0.45),
        ("mode4/clap",   "Idiophones/Struck Idiophones/Claps/Clap_rr1.wav", 0.3),
    ]
    
    for dest_name, rel_path, dur in percussions:
        url = VCSL_BASE + urllib.parse.quote(rel_path)
        cached_file = download_cache(url, os.path.basename(rel_path))
        data, sr = sf.read(cached_file)
        export_wav(f"{OUT_DIR}/{dest_name}.wav", data, sr, max_dur=dur)

# ============================================================================
# 4. MODE 5: TYPEWRITER (VCSL / CC0)
# ============================================================================
def build_mode5():
    print("\n=== Building Mode 5: Vintage Typewriter ===")
    VCSL_BASE = "https://raw.githubusercontent.com/sgossner/VCSL/master/"
    
    tw_samples = [
        ("key_click", "Idiophones/Struck Idiophones/Woodblock/wood_click_f_rr1.wav", 0.08),
        ("space",     "Idiophones/Struck Idiophones/Woodblock/wood_click2_mp.wav", 0.15),
        ("bell",      "Idiophones/Struck Idiophones/Glockenspiel/glock_loud_C7_01.wav", 0.8),
        ("backspace", "Idiophones/Struck Idiophones/Woodblock/wood_click3_vl1.wav", 0.12),
    ]
    for dest_name, rel_path, dur in tw_samples:
        url = VCSL_BASE + urllib.parse.quote(rel_path)
        cached_file = download_cache(url, f"tw_{os.path.basename(rel_path)}")
        data, sr = sf.read(cached_file)
        export_wav(f"{OUT_DIR}/mode5/{dest_name}.wav", data, sr, max_dur=dur)

# ============================================================================
# 5. MODE 6: CARTOON SFX (VCSL & CC0 WHISTLE/BELLS)
# ============================================================================
def build_mode6():
    print("\n=== Building Mode 6: Cartoon SFX ===")
    VCSL_BASE = "https://raw.githubusercontent.com/sgossner/VCSL/master/"
    
    cartoon_samples = [
        ("coin",     "Idiophones/Struck Idiophones/Glockenspiel/glock_loud_C7_01.wav", 0.4, 0.0),
        ("whistle",  "Aerophones/Edge-blown Aerophones/Train Whistle, Toy/Main_TrainLow_Double-001.wav", 1.2, 0.0),
        ("doorbell", "Idiophones/Struck Idiophones/Glockenspiel/glock_loud_C5_01.wav", 0.7, 0.0),
    ]
    for dest_name, rel_path, dur, shift in cartoon_samples:
        url = VCSL_BASE + urllib.parse.quote(rel_path)
        cached_file = download_cache(url, f"ct_{os.path.basename(rel_path)}")
        data, sr = sf.read(cached_file)
        export_wav(f"{OUT_DIR}/mode6/{dest_name}.wav", data, sr, max_dur=dur, semitone_shift=shift)

def main():
    print("==================================================")
    print("    ESP32-S3 Soundbank Asset Builder (24 kHz)     ")
    print("==================================================")
    build_mode9()
    build_mode1()
    build_mode4_and_common()
    build_mode5()
    build_mode6()
    print("\n>>> ALL ASSETS BUILT SUCCESSFULLY!")

if __name__ == "__main__":
    main()
