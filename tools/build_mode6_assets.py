import os
import urllib.request
import urllib.parse
import numpy as np
import soundfile as sf
import scipy.signal as sps

SR = 24000
OUT_DIR = "audio_assets/mode6"
CACHE_DIR = "tools/_cache"
VCSL_BASE = "https://raw.githubusercontent.com/sgossner/VCSL/master/"

os.makedirs(OUT_DIR, exist_ok=True)
os.makedirs(CACHE_DIR, exist_ok=True)

def download_cache(rel_path, filename):
    filepath = os.path.join(CACHE_DIR, filename)
    if os.path.exists(filepath) and os.path.getsize(filepath) > 0:
        return filepath
    url = VCSL_BASE + urllib.parse.quote(rel_path)
    print(f"Downloading {filename} from VCSL...")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (ESP32-Audio-Builder)"})
    with urllib.request.urlopen(req) as resp, open(filepath, "wb") as out:
        out.write(resp.read())
    return filepath

def export_wav(dest_path, audio_arr, max_dur=None, peak_gain=0.88, decay_rate=0.0):
    audio_arr = np.asarray(audio_arr, dtype=np.float32)
    if audio_arr.ndim > 1:
        audio_arr = np.mean(audio_arr, axis=1)

    # Trim leading silence
    thresh = 0.005
    active = np.where(np.abs(audio_arr) > thresh)[0]
    if len(active) > 0:
        audio_arr = audio_arr[max(0, active[0] - int(SR * 0.005)):]

    # Optional acoustic decay
    if decay_rate > 0.0:
        n_att = int(SR * 0.03)
        t = np.arange(len(audio_arr)) / SR
        env = np.ones(len(audio_arr), dtype=np.float32)
        if len(audio_arr) > n_att:
            env[n_att:] = np.exp(-decay_rate * (t[n_att:] - 0.03))
        audio_arr = audio_arr * env

    # Duration limit
    if max_dur:
        max_samples = int(SR * max_dur)
        if len(audio_arr) > max_samples:
            audio_arr = audio_arr[:max_samples]
            fo = min(len(audio_arr), int(SR * 0.06))
            if fo > 0:
                audio_arr[-fo:] *= np.linspace(1.0, 0.0, fo)

    # Soft 2ms fade-in
    fi = min(len(audio_arr), int(SR * 0.002))
    if fi > 0:
        audio_arr[:fi] *= np.linspace(0.0, 1.0, fi)

    # DC removal & normalize
    audio_arr -= np.mean(audio_arr)
    pk = np.max(np.abs(audio_arr))
    if pk > 1e-4:
        audio_arr = audio_arr * (peak_gain / pk)

    pcm16 = (np.clip(audio_arr, -1.0, 1.0) * 32767.0).astype(np.int16)
    sf.write(dest_path, pcm16, SR, subtype="PCM_16")
    dur = len(pcm16) / SR
    print(f" -> Saved {dest_path}: {len(pcm16)} samples ({dur:.2f}s, {len(pcm16)*2/1024:.1f} KB)")

# ============================================================================
# PROCEDURAL SYNTHESIZERS (RETRO GAME & CARTOON FX)
# ============================================================================

def gen_coin():
    # Authentic NES Mario Coin
    t1 = np.linspace(0, 0.07, int(SR * 0.07), False)
    s1 = np.sign(np.sin(2 * np.pi * 987.77 * t1)) * 0.8
    t2 = np.linspace(0, 0.32, int(SR * 0.32), False)
    env2 = np.exp(-4.5 * t2 / 0.32)
    s2 = np.sign(np.sin(2 * np.pi * 1318.51 * t2)) * env2 * 0.85
    return np.concatenate([s1, s2])

def gen_jump():
    dur = 0.22
    t = np.linspace(0, dur, int(SR * dur), False)
    freq = 140.0 + 380.0 * (1.0 - np.exp(-14.0 * t))
    phase = 2 * np.pi * np.cumsum(freq) / SR
    sq = np.sign(np.sin(phase))
    env = np.linspace(1.0, 0.15, len(t)) ** 1.3
    return sq * env

def gen_powerup():
    notes = [329.63, 392.00, 659.25, 523.25, 587.33, 783.99]
    parts = []
    dur = 0.075
    for f in notes:
        t = np.linspace(0, dur, int(SR * dur), False)
        sq = np.sign(np.sin(2 * np.pi * f * t))
        env = np.exp(-3.0 * t / dur)
        parts.append(sq * env)
    return np.concatenate(parts)

def gen_1up():
    notes = [(659.25, 0.08), (783.99, 0.08), (1318.51, 0.08), (1046.50, 0.08), (1174.66, 0.08), (1567.98, 0.18)]
    parts = []
    for f, d in notes:
        t = np.linspace(0, d, int(SR * d), False)
        sq = np.sign(np.sin(2 * np.pi * f * t))
        env = np.exp(-3.5 * t / d)
        parts.append(sq * env)
        parts.append(np.zeros(int(SR * 0.01)))
    return np.concatenate(parts)

def gen_fireball():
    dur = 0.09
    t = np.linspace(0, dur, int(SR * dur), False)
    freq = 950.0 * np.exp(-18.0 * t) + 160.0
    phase = 2 * np.pi * np.cumsum(freq) / SR
    sq = np.sign(np.sin(phase)) + 0.3 * np.sin(phase)
    env = np.exp(-15.0 * t)
    return sq * env

def gen_warp():
    parts = []
    for _ in range(2):
        for f in [350.0, 260.0, 180.0, 130.0]:
            dur = 0.055
            t = np.linspace(0, dur, int(SR * dur), False)
            sq = np.sign(np.sin(2 * np.pi * f * t))
            env = np.exp(-4.0 * t / dur)
            parts.append(sq * env)
    return np.concatenate(parts)

def gen_laser():
    dur = 0.16
    t = np.linspace(0, dur, int(SR * dur), False)
    freq = 3200.0 * np.exp(-22.0 * t) + 160.0
    phase = 2 * np.pi * np.cumsum(freq) / SR
    sq = np.sign(np.sin(phase)) + 0.4 * np.sin(phase)
    env = np.exp(-12.0 * t)
    return sq * env

def gen_bomb():
    dur = 0.65
    t = np.linspace(0, dur, int(SR * dur), False)
    noise = np.random.uniform(-1.0, 1.0, len(t))
    b, a = sps.butter(2, 600.0 / (SR / 2), btype='low')
    filt_noise = sps.lfilter(b, a, noise)
    sub = np.sin(2 * np.pi * (65.0 * np.exp(-3.5 * t) + 30.0) * t) * np.exp(-2.5 * t)
    env = np.minimum(t / 0.005, 1.0) * np.exp(-3.2 * t)
    return 0.7 * filt_noise * env + 0.8 * sub * env

def gen_pacman():
    parts = []
    for f_start, f_end in [(360.0, 140.0), (420.0, 160.0), (360.0, 140.0), (420.0, 160.0)]:
        dur = 0.08
        t = np.linspace(0, dur, int(SR * dur), False)
        freq = f_start + (f_end - f_start) * (t / dur)
        phase = 2 * np.pi * np.cumsum(freq) / SR
        tri = 2.0 * np.abs(2.0 * (phase / (2 * np.pi) - np.floor(phase / (2 * np.pi) + 0.5))) - 1.0
        env = np.sin(np.pi * t / dur) ** 0.8
        parts.append(tri * env)
        parts.append(np.zeros(int(SR * 0.015)))
    return np.concatenate(parts)

def gen_sonic():
    dur = 0.42
    t = np.linspace(0, dur, int(SR * dur), False)
    rev = 220.0 + 720.0 * (t / dur) ** 1.6
    vib = 30.0 * np.sin(2 * np.pi * 28.0 * t)
    phase = 2 * np.pi * np.cumsum(rev + vib) / SR
    saw = 2.0 * (phase / (2 * np.pi) - np.floor(phase / (2 * np.pi) + 0.5))
    env = np.minimum(t / 0.03, 1.0) * (0.8 + 0.2 * np.sin(2 * np.pi * 28.0 * t))
    return saw * env

def gen_punch():
    dur = 0.28
    t = np.linspace(0, dur, int(SR * dur), False)
    thump = np.sin(2 * np.pi * (130.0 * np.exp(-18.0 * t) + 45.0) * t) * np.exp(-8.0 * t)
    noise = np.random.uniform(-1.0, 1.0, len(t))
    b, a = sps.butter(2, [180.0 / (SR / 2), 2200.0 / (SR / 2)], btype='band')
    slap = sps.lfilter(b, a, noise) * np.exp(-22.0 * t)
    return thump * 0.8 + slap * 0.7

def gen_fanfare():
    # Royal Victory Fanfare
    notes = [(523.25, 0.10), (659.25, 0.10), (783.99, 0.10), (1046.50, 0.40)]
    parts = []
    for f, d in notes:
        t = np.linspace(0, d, int(SR * d), False)
        env = np.minimum(t / 0.015, 1.0) * np.exp(-1.8 * t / d)
        brass = (np.sin(2 * np.pi * f * t) + 0.5 * np.sin(4 * np.pi * f * t) + 0.25 * np.sin(6 * np.pi * f * t)) * env
        parts.append(brass)
    return np.concatenate(parts)

def gen_sad_trombone():
    notes = [(311.13, 0.22), (293.66, 0.22), (277.18, 0.22), (261.63, 0.65)]
    parts = []
    for i, (f, d) in enumerate(notes):
        t = np.linspace(0, d, int(SR * d), False)
        vib = 1.0 + (0.04 * np.sin(2 * np.pi * 5.5 * t) if i == 3 else 0.0)
        bend = 1.0 - (0.08 * (t / d) if i == 3 else 0.0)
        freq = f * vib * bend
        phase = 2 * np.pi * np.cumsum(freq) / SR
        brass = np.sin(phase) + 0.6 * np.sin(2 * phase) + 0.35 * np.sin(3 * phase) + 0.2 * np.sin(4 * phase)
        env = np.minimum(t / 0.03, 1.0) * np.exp(-1.5 * t / d)
        parts.append(brass * env)
        parts.append(np.zeros(int(SR * 0.04)))
    return np.concatenate(parts)

def gen_slip_fall():
    # Whistle swoop down
    dur1 = 0.24
    t1 = np.linspace(0, dur1, int(SR * dur1), False)
    freq1 = 1900.0 * np.exp(-9.0 * t1) + 250.0
    phase1 = 2 * np.pi * np.cumsum(freq1) / SR
    whistle = (np.sin(phase1) + 0.2 * np.sin(2 * phase1)) * np.minimum(t1 / 0.02, 1.0) * np.exp(-2.0 * t1)
    
    # Impact crash
    dur2 = 0.35
    t2 = np.linspace(0, dur2, int(SR * dur2), False)
    noise = np.random.uniform(-1.0, 1.0, len(t2))
    b, a = sps.butter(2, [120.0 / (SR / 2), 1800.0 / (SR / 2)], btype='band')
    crash = sps.lfilter(b, a, noise) * np.exp(-12.0 * t2)
    thud = np.sin(2 * np.pi * (110.0 * np.exp(-15.0 * t2) + 40.0) * t2) * np.exp(-7.0 * t2)
    impact = 0.7 * crash + 0.8 * thud
    return np.concatenate([whistle, impact])

def gen_boing():
    dur = 0.38
    t = np.linspace(0, dur, int(SR * dur), False)
    freq = 180.0 + 460.0 * (t / dur) ** 0.6 + 32.0 * np.sin(2 * np.pi * 22.0 * t)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.exp(-4.2 * t)
    s = (np.sin(phase) + 0.4 * np.sin(2 * phase) + 0.15 * np.sin(3 * phase)) * env
    return s

def gen_squeak_duck():
    dur = 0.12
    t = np.linspace(0, dur, int(SR * dur), False)
    freq1 = 1350.0 + 350.0 * np.sin(np.pi * t / dur)
    p1 = 2 * np.pi * np.cumsum(freq1) / SR
    s1 = (np.sin(p1) + 0.4 * np.sin(3 * p1)) * np.sin(np.pi * t / dur) ** 1.2
    gap = np.zeros(int(SR * 0.04))
    freq2 = 1600.0 + 400.0 * np.sin(np.pi * t / dur)
    p2 = 2 * np.pi * np.cumsum(freq2) / SR
    s2 = (np.sin(p2) + 0.4 * np.sin(3 * p2)) * np.sin(np.pi * t / dur) ** 1.2 * 0.85
    return np.concatenate([s1, gap, s2])

def gen_horn():
    # Double honk "Bim bim"
    def single_honk():
        dur = 0.14
        t = np.linspace(0, dur, int(SR * dur), False)
        env = np.minimum(t / 0.015, 1.0) * np.exp(-2.0 * t)
        h = (np.sin(2 * np.pi * 370.0 * t) + 0.8 * np.sin(2 * np.pi * 466.0 * t) + 0.3 * np.sin(2 * np.pi * 740.0 * t)) * env
        return h
    h1 = single_honk()
    gap = np.zeros(int(SR * 0.05))
    h2 = single_honk() * 0.95
    return np.concatenate([h1, gap, h2])

def gen_splat():
    dur = 0.22
    t = np.linspace(0, dur, int(SR * dur), False)
    noise = np.random.uniform(-1.0, 1.0, len(t))
    b, a = sps.butter(2, [300.0 / (SR / 2), 2800.0 / (SR / 2)], btype='band')
    squish = sps.lfilter(b, a, noise) * np.exp(-14.0 * t)
    bubble = np.sin(2 * np.pi * (450.0 * np.exp(-12.0 * t) + 120.0) * t) * np.exp(-8.0 * t)
    return squish * 0.8 + bubble * 0.5

def gen_magic_wand():
    notes = [1046.50, 1318.51, 1567.98, 1975.53, 2093.00, 2637.02, 3135.96]
    parts = []
    dur = 0.07
    for f in notes:
        t = np.linspace(0, dur, int(SR * dur), False)
        chime = np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t)
        env = np.exp(-5.0 * t / dur)
        parts.append(chime * env)
    t_end = np.linspace(0, 0.45, int(SR * 0.45), False)
    shimmer = (np.sin(2 * np.pi * 3135.96 * t_end) + 0.5 * np.sin(2 * np.pi * 4186.01 * t_end)) * np.exp(-5.5 * t_end)
    parts.append(shimmer)
    return np.concatenate(parts)

def gen_water_drop():
    dur = 0.16
    t = np.linspace(0, dur, int(SR * dur), False)
    freq = 650.0 + 1750.0 * (t / dur) ** 2.2
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.sin(np.pi * t / dur) ** 0.8 * np.exp(-12.0 * t)
    return np.sin(phase) * env

def gen_ghost():
    dur = 0.75
    t = np.linspace(0, dur, int(SR * dur), False)
    freq = 320.0 + 120.0 * np.sin(2 * np.pi * 5.0 * t) + 60.0 * (t / dur)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.minimum(t / 0.15, 1.0) * np.exp(-1.5 * t)
    return (np.sin(phase) + 0.25 * np.sin(2 * phase)) * env

def gen_run_away():
    dur = 0.35
    t = np.linspace(0, dur, int(SR * dur), False)
    freq = 240.0 + 2200.0 * (t / dur) ** 2.0
    phase = 2 * np.pi * np.cumsum(freq) / SR
    noise = np.random.uniform(-0.3, 0.3, len(t))
    env = np.minimum(t / 0.05, 1.0) * np.maximum(1.0 - (t / dur), 0.0)
    return (np.sin(phase) + noise) * env

def gen_skid():
    dur = 0.35
    t = np.linspace(0, dur, int(SR * dur), False)
    noise = np.random.uniform(-1, 1, len(t))
    freq = 2200.0 * np.exp(-4.0 * t) + 400.0
    phase = 2 * np.pi * np.cumsum(freq) / SR
    screech = np.sin(phase) + 0.4 * np.sin(2.1 * phase)
    env = np.minimum(t / 0.03, 1.0) * np.exp(-3.5 * t)
    return (0.7 * screech + 0.5 * noise) * env

def main():
    print("=================================================================")
    print("  BUILDING MODE 6 HIGH-DEFINITION CARTOON & RETRO AUDIO ASSETS  ")
    print("=================================================================")

    # 1. Download & process real acoustic VCSL instruments
    vcsl_items = [
        ("anvil.wav",        "Idiophones/Struck Idiophones/Anvil/Anvil_Hit1_v3_rr1_Mid.wav", 0.75, 1.2),
        ("slapstick.wav",    "Idiophones/Struck Idiophones/Slapstick/slapstick_rr1.wav", 0.35, 0.0),
        ("flexatone.wav",    "Idiophones/Struck Idiophones/Flexatone/flexatone1.wav", 0.85, 0.5),
        ("vibraslap.wav",    "Idiophones/Struck Idiophones/Vibraslap/Legacy/vibraslap_rr1.wav", 0.85, 0.6),
        ("ball_whistle.wav", "Aerophones/Edge-blown Aerophones/Ball Whistle/Main_BallWhistle_Short-001.wav", 0.45, 0.0),
        ("cowbell.wav",      "Idiophones/Struck Idiophones/Cowbells/Cowbell1_Hit_v3_rr1_Mid.wav", 0.40, 1.5),
        ("timpani.wav",      "Membranophones/Struck Membranophones/Timpani 1/Hit/Timpani1_Hit_v3_rr3_Sum.wav", 0.85, 0.8),
        ("siren.wav",        "Aerophones/Free Aerophones/Siren/Main_SirenWhistle-008.wav", 1.20, 0.0),
        ("doorbell.wav",     "Idiophones/Struck Idiophones/Glockenspiel/glock_loud_C5_01.wav", 0.70, 1.5),
        ("whistle.wav",      "Aerophones/Edge-blown Aerophones/Train Whistle, Toy/Main_TrainLow_Double-001.wav", 1.20, 0.0),
    ]

    for out_name, rel_path, max_d, decay in vcsl_items:
        fn = os.path.basename(rel_path)
        cached = download_cache(rel_path, fn)
        d, sr = sf.read(cached)
        if sr != SR:
            if d.ndim > 1: d = np.mean(d, axis=1)
            d = sps.resample_poly(d, SR, sr)
        export_wav(f"{OUT_DIR}/{out_name}", d, max_dur=max_d, decay_rate=decay)

    # 2. Tom & Jerry Bongo Scramble Feet (built from real bongo sample)
    bongo_f = download_cache("Membranophones/Struck Membranophones/Bongos/BongoH_Hit1_v2_rr1_Mid.wav", "BongoH_Hit1_v2_rr1_Mid.wav")
    d, sr = sf.read(bongo_f)
    if d.ndim > 1: d = np.mean(d, axis=1)
    if sr != SR: d = sps.resample_poly(d, SR, sr)
    hit = d[:int(SR * 0.08)]
    bongo_parts = []
    for i in range(8):
        shift = 1.0 if (i % 2 == 0) else 1.28
        h = sps.resample_poly(hit, int(100 * shift), 100)
        bongo_parts.append(h)
        gap = int(SR * max(0.015, 0.045 - i * 0.0035))
        bongo_parts.append(np.zeros(gap))
    export_wav(f"{OUT_DIR}/bongo_run.wav", np.concatenate(bongo_parts), max_dur=0.75)

    # 3. Procedural Synthesized Sounds
    synths = [
        ("coin.wav",         gen_coin(),         0.40),
        ("jump.wav",         gen_jump(),         0.25),
        ("powerup.wav",      gen_powerup(),      0.50),
        ("1up.wav",          gen_1up(),          0.65),
        ("fireball.wav",     gen_fireball(),     0.12),
        ("warp.wav",         gen_warp(),         0.48),
        ("laser.wav",        gen_laser(),        0.18),
        ("bomb.wav",         gen_bomb(),         0.65),
        ("pacman.wav",       gen_pacman(),       0.45),
        ("sonic.wav",        gen_sonic(),        0.45),
        ("punch.wav",        gen_punch(),        0.30),
        ("fanfare.wav",      gen_fanfare(),      0.75),
        ("sad_trombone.wav", gen_sad_trombone(), 1.60),
        ("slip_fall.wav",    gen_slip_fall(),    0.65),
        ("boing.wav",        gen_boing(),        0.42),
        ("squeak_duck.wav",  gen_squeak_duck(),  0.32),
        ("horn.wav",         gen_horn(),         0.38),
        ("splat.wav",        gen_splat(),        0.25),
        ("magic_wand.wav",   gen_magic_wand(),   0.90),
        ("water_drop.wav",   gen_water_drop(),   0.20),
        ("ghost.wav",        gen_ghost(),        0.80),
        ("run_away.wav",     gen_run_away(),     0.38),
        ("skid.wav",         gen_skid(),         0.38),
    ]

    for out_name, arr, max_d in synths:
        export_wav(f"{OUT_DIR}/{out_name}", arr, max_dur=max_d)

    print("\n>>> ALL MODE 6 ASSETS SUCCESSFULLY BUILT AND OPTIMIZED!")

if __name__ == "__main__":
    main()
