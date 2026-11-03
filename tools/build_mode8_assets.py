import os
import sys
import io
import urllib.request
import numpy as np
import soundfile as sf
import scipy.signal as sps
from piper.voice import PiperVoice

CACHE_DIR = "tools/_cache/mode8"
OUT_DIR = "audio_assets/mode8"
TARGET_SR = 24000
ONNX_PATH = "tools/_cache/vi_VN-vais1000-medium.onnx"

os.makedirs(CACHE_DIR, exist_ok=True)
os.makedirs(OUT_DIR, exist_ok=True)

def download_cache(url, filename):
    filepath = os.path.join(CACHE_DIR, filename)
    if os.path.exists(filepath) and os.path.getsize(filepath) > 500:
        return filepath
    print(f"Downloading {url} ...")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"})
    with urllib.request.urlopen(req, timeout=20) as resp, open(filepath, "wb") as out:
        out.write(resp.read())
    return filepath

def export_wav(dest_path, audio_arr, sr, max_dur=None, semitone_shift=0.0, peak_gain=0.88, start_offset_s=0.0):
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    if audio_arr.ndim > 1:
        audio_arr = np.mean(audio_arr, axis=1) # Mono

    # Start offset
    if start_offset_s > 0.0:
        offset_samples = int(sr * start_offset_s)
        if offset_samples < len(audio_arr):
            audio_arr = audio_arr[offset_samples:]

    # Pitch shift if needed
    if semitone_shift != 0.0:
        ratio = 2.0 ** (-semitone_shift / 12.0)
        up = int(np.round(1000 * ratio))
        down = 1000
        audio_arr = sps.resample_poly(audio_arr, up, down)

    # Resample to TARGET_SR
    if sr != TARGET_SR:
        audio_arr = sps.resample_poly(audio_arr, TARGET_SR, sr)

    # Trim leading silence
    thresh = 0.010
    active = np.where(np.abs(audio_arr) > thresh)[0]
    if len(active) > 0:
        start_idx = max(0, active[0] - int(TARGET_SR * 0.01))
        audio_arr = audio_arr[start_idx:]

    # Trim duration if requested
    if max_dur:
        max_samples = int(TARGET_SR * max_dur)
        if len(audio_arr) > max_samples:
            audio_arr = audio_arr[:max_samples]
            fo = min(len(audio_arr), int(TARGET_SR * 0.12))
            if fo > 0:
                audio_arr[-fo:] *= np.linspace(1.0, 0.0, fo)

    # Soft fade in
    fi = min(len(audio_arr), int(TARGET_SR * 0.005))
    if fi > 0:
        audio_arr[:fi] *= np.linspace(0.0, 1.0, fi)

    # Soft fade out at end
    fo = min(len(audio_arr), int(TARGET_SR * 0.025))
    if fo > 0:
        audio_arr[-fo:] *= np.linspace(1.0, 0.0, fo)

    # Remove DC offset
    audio_arr -= np.mean(audio_arr)

    # Peak normalize
    pk = np.max(np.abs(audio_arr))
    if pk > 0.001:
        audio_arr = audio_arr * (peak_gain / pk)

    pcm16 = (np.clip(audio_arr, -1.0, 1.0) * 32767.0).astype(np.int16)
    sf.write(dest_path, pcm16, TARGET_SR, subtype="PCM_16")
    dur = len(pcm16) / TARGET_SR
    print(f" -> Saved {dest_path} ({dur:.2f}s, {len(pcm16)*2/1024:.1f} KB)")

# Synthesized SFX helpers
def generate_reverse_beep():
    dur = 0.8
    t = np.linspace(0, dur, int(TARGET_SR * dur), False)
    beep_len = int(TARGET_SR * 0.22)
    sig = np.zeros_like(t)
    for start in [0, int(TARGET_SR * 0.40)]:
        bt = np.linspace(0, 0.22, beep_len, False)
        env = np.sin(np.pi * bt / 0.22)
        sig[start:start+beep_len] = np.sin(2 * np.pi * 1250.0 * bt) * env
    return sig

def generate_turn_signal():
    dur = 0.6
    t = np.linspace(0, dur, int(TARGET_SR * dur), False)
    sig = np.zeros_like(t)
    for start in [0, int(TARGET_SR * 0.28)]:
        clen = int(TARGET_SR * 0.025)
        ct = np.linspace(0, 0.025, clen, False)
        cenv = np.exp(-180.0 * ct)
        noise = np.random.uniform(-0.5, 0.5, clen)
        click = (np.sin(2 * np.pi * 2400.0 * ct) + noise) * cenv
        sig[start:start+clen] = click
    return sig

def generate_unicorn_magic():
    dur = 1.3
    t = np.linspace(0, dur, int(TARGET_SR * dur), False)
    sig = np.zeros_like(t)
    notes = [1046.5, 1318.5, 1567.98, 2093.0, 2637.0, 3135.96]
    for i, freq in enumerate(notes):
        start = int(TARGET_SR * (i * 0.12))
        nlen = int(TARGET_SR * 0.45)
        if start + nlen > len(t):
            nlen = len(t) - start
        nt = np.linspace(0, nlen / TARGET_SR, nlen, False)
        env = np.exp(-12.0 * nt)
        bell = (np.sin(2 * np.pi * freq * nt) + 0.3 * np.sin(2 * np.pi * freq * 2.75 * nt)) * env
        sig[start:start+nlen] += bell * 0.5
    return sig

def generate_skid():
    dur = 0.75
    t = np.linspace(0, dur, int(TARGET_SR * dur), False)
    noise = np.random.uniform(-0.4, 0.4, len(t))
    freq = 2400.0 * np.exp(-3.5 * t) + 450.0
    phase = 2 * np.pi * np.cumsum(freq) / TARGET_SR
    screech = np.sin(phase) + 0.3 * np.sin(2.1 * phase)
    env = np.minimum(t / 0.03, 1.0) * np.exp(-3.0 * t)
    return (0.7 * screech + 0.5 * noise) * env

def generate_rocket():
    dur = 1.1
    t = np.linspace(0, dur, int(TARGET_SR * dur), False)
    noise = np.random.uniform(-0.6, 0.6, len(t))
    freq = 110.0 + 800.0 * (t / dur)**1.8
    phase = 2 * np.pi * np.cumsum(freq) / TARGET_SR
    rumble = np.sin(phase)
    env = np.minimum(t / 0.3, 1.0) * np.maximum(1.0 - (t - 0.7) / 0.4, 0.2)
    return (0.6 * rumble + 0.6 * noise) * env

def build_piper_speech(voice, text, intro_beep=True):
    chunks = list(voice.synthesize(text))
    raw = b''.join(c.audio_int16_bytes for c in chunks)
    arr = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
    if voice.config.sample_rate != TARGET_SR:
        arr = sps.resample_poly(arr, TARGET_SR, voice.config.sample_rate)

    if intro_beep:
        # Walkie-talkie chirp beep (100ms)
        bt = np.linspace(0, 0.09, int(TARGET_SR * 0.09), False)
        beep = np.sin(2 * np.pi * 1750.0 * bt) * np.sin(np.pi * bt / 0.09) * 0.5
        pause = np.zeros(int(TARGET_SR * 0.05), dtype=np.float32)
        arr = np.concatenate([beep, pause, arr])
    return arr

def main():
    print("=== 1. PREPARING DATASET SOURCES ===")
    ESC_BASE = "https://raw.githubusercontent.com/karolpiczak/ESC-50/master/audio/"
    ALFA_BASE = "https://raw.githubusercontent.com/prashant-andani/alfaKidz/master/src/assets/animals/"
    RIDH_BASE = "https://raw.githubusercontent.com/ridhurshan/Animal-Soundboard/master/static/sound/"

    # 26 Animals (A to Z)
    animal_sources = {
        'a': (RIDH_BASE + "Alligator.mp3", "Alligator.mp3", 1.8, 0.0, 0.0),
        'b': (ESC_BASE + "1-100038-A-14.wav", "esc_bird.wav", 1.8, 0.0, 0.0),
        'c': (ESC_BASE + "1-34094-A-5.wav", "esc_cat.wav", 1.6, 0.0, 0.0),
        'd': (ESC_BASE + "1-100032-A-0.wav", "esc_dog.wav", 1.6, 0.0, 0.0),
        'e': (ALFA_BASE + "elephant.wav", "alfa_elephant.wav", 1.8, 0.0, 0.0),
        'f': (ESC_BASE + "1-15689-A-4.wav", "esc_frog.wav", 1.5, 0.0, 0.0),
        'g': (ALFA_BASE + "goat.wav", "alfa_goat.wav", 1.5, 0.0, 0.0),
        'h': (ALFA_BASE + "horse.wav", "alfa_horse.wav", 1.8, 0.0, 0.0),
        'i': (ESC_BASE + "1-57316-A-13.wav", "esc_crickets.wav", 1.6, 0.0, 0.0),
        'j': (ALFA_BASE + "jaguar.wav", "alfa_jaguar.wav", 1.8, 0.0, 0.0),
        'k': (RIDH_BASE + "Koala.mp3", "ridh_koala.mp3", 1.6, 0.0, 0.0),
        'l': (ALFA_BASE + "lion.wav", "alfa_lion.wav", 1.8, 0.0, 0.0),
        'm': (ALFA_BASE + "monkey.wav", "alfa_monkey.wav", 1.6, 0.0, 0.0),
        'n': (RIDH_BASE + "Parrot.mp3", "ridh_parrot.mp3", 1.6, 0.0, 0.0),
        'o': (RIDH_BASE + "Owl.mp3", "ridh_owl.mp3", 1.8, 0.0, 0.0),
        'p': (ESC_BASE + "1-208757-A-2.wav", "esc_pig.wav", 1.5, 0.0, 0.0),
        'q': (RIDH_BASE + "Duck.mp3", "ridh_duck.mp3", 1.5, 0.0, 0.0),
        'r': (ESC_BASE + "1-26806-A-1.wav", "esc_rooster.wav", 1.8, 0.0, 0.0),
        's': (ESC_BASE + "1-121951-A-8.wav", "esc_sheep.wav", 1.6, 0.0, 0.0),
        't': (ALFA_BASE + "tiger.wav", "alfa_tiger.wav", 1.8, 0.0, 0.0),
        'u': (None, None, 1.4, 0.0, 0.0), # Unicorn magic bell
        'v': (RIDH_BASE + "Hawk.mp3", "ridh_hawk.mp3", 1.7, 0.0, 0.0),
        'w': (RIDH_BASE + "Wolf.mp3", "ridh_wolf.mp3", 2.0, 0.0, 0.0),
        'x': (RIDH_BASE + "cheetah.mp3", "ridh_cheetah.mp3", 1.6, 0.0, 0.0),
        'y': (ESC_BASE + "1-16568-A-3.wav", "esc_cow.wav", 1.8, -4.0, 0.0), # Yak / deep bull
        'z': (ALFA_BASE + "zebra.wav", "alfa_zebra.wav", 1.8, 0.0, 0.0),
    }

    print("\n=== 2. PROCESSING 26 ANIMAL SOUNDS (A-Z) ===")
    for char, (url, cache_file, max_dur, pitch_shift, offset) in animal_sources.items():
        dest = f"{OUT_DIR}/animal_{char}.wav"
        if char == 'u':
            # Unicorn
            arr = generate_unicorn_magic()
            export_wav(dest, arr, TARGET_SR, max_dur=max_dur)
            continue

        try:
            cf = download_cache(url, cache_file)
            arr, sr = sf.read(cf)
            export_wav(dest, arr, sr, max_dur=max_dur, semitone_shift=pitch_shift, start_offset_s=offset)
        except Exception as e:
            print(f"Error processing animal_{char}: {e}")

    print("\n=== 3. PROCESSING COCKPIT SFX ===")
    cockpit_sources = [
        ("siren.wav",      ESC_BASE + "1-31482-A-42.wav",  "esc_siren.wav",      2.2),
        ("horn.wav",       ESC_BASE + "1-17124-A-43.wav",  "esc_horn.wav",       1.2),
        ("engine.wav",     ESC_BASE + "1-18527-A-44.wav",  "esc_engine.wav",     1.8),
        ("water.wav",      ESC_BASE + "1-118559-A-17.wav", "esc_water.wav",      1.8),
        ("fire.wav",       ESC_BASE + "1-17150-A-12.wav",  "esc_fire.wav",       1.8),
        ("helicopter.wav", ESC_BASE + "1-172649-A-40.wav", "esc_helicopter.wav", 1.8),
        ("cheer.wav",      ESC_BASE + "1-104089-A-22.wav", "esc_clapping.wav",   1.8),
        ("bell.wav",       ESC_BASE + "1-13571-A-46.wav",  "esc_bell.wav",       1.5),
    ]

    for name, url, cache_file, max_dur in cockpit_sources:
        dest = f"{OUT_DIR}/{name}"
        try:
            cf = download_cache(url, cache_file)
            arr, sr = sf.read(cf)
            export_wav(dest, arr, sr, max_dur=max_dur)
        except Exception as e:
            print(f"Error processing {name}: {e}")

    # Synthesized vehicle SFX
    export_wav(f"{OUT_DIR}/skid.wav", generate_skid(), TARGET_SR, max_dur=0.75)
    export_wav(f"{OUT_DIR}/rocket.wav", generate_rocket(), TARGET_SR, max_dur=1.1)
    export_wav(f"{OUT_DIR}/reverse.wav", generate_reverse_beep(), TARGET_SR, max_dur=0.8)
    export_wav(f"{OUT_DIR}/turn.wav", generate_turn_signal(), TARGET_SR, max_dur=0.6)

    print("\n=== 4. PROCESSING RESCUE RADIO & MISSION PROMPTS (PIPER TTS) ===")
    v = PiperVoice.load(ONNX_PATH)
    radio_arr = build_piper_speech(v, "Báo động một một bốn! Xe cứu hỏa xuất kích!")
    export_wav(f"{OUT_DIR}/radio.wav", radio_arr, TARGET_SR, max_dur=2.6)

    mission_arr = build_piper_speech(v, "Báo động cứu hộ! Lắng nghe tiếng bạn thú nào!")
    export_wav(f"{OUT_DIR}/mission.wav", mission_arr, TARGET_SR, max_dur=2.8)

    success_arr = build_piper_speech(v, "Hoan hô! Cứu hộ thành công rồi!")
    export_wav(f"{OUT_DIR}/success.wav", success_arr, TARGET_SR, max_dur=2.4)

    print("\n>>> ALL MODE 8 HIGH-FIDELITY ASSETS GENERATED SUCCESSFULLY! <<<")

if __name__ == '__main__':
    main()
