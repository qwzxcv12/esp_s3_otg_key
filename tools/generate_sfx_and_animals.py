import os
import numpy as np
import soundfile as sf

SR = 24000

def save_wav(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # Peak normalize to -1.5 dB (0.85) to avoid any clipping
    data = np.asarray(data, dtype=np.float32)
    mx = np.max(np.abs(data))
    if mx > 1e-4:
        data = (data / mx) * 0.85
    sf.write(path, data, SR, subtype='PCM_16')
    print(f"Saved: {path} ({len(data)} samples, {len(data)/SR:.2f}s)")

# ----------------- Mode 6 SFX -----------------
def gen_boing():
    t = np.linspace(0, 0.45, int(SR * 0.45), False)
    # Pitch sweep up with spring wobble
    freq = 200.0 + 500.0 * (t / 0.45)**0.7 + 35.0 * np.sin(2 * np.pi * 24.0 * t)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.exp(-4.5 * t)
    s = (np.sin(phase) + 0.4 * np.sin(2 * phase) + 0.15 * np.sin(3 * phase)) * env
    return s

def gen_laser():
    t = np.linspace(0, 0.18, int(SR * 0.18), False)
    # Sci-fi laser: rapid downward pitch sweep
    freq = 3200.0 * np.exp(-22.0 * t) + 180.0
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.exp(-12.0 * t)
    s = (np.sin(phase) + 0.5 * np.sign(np.sin(phase))) * env
    return s

def gen_horn():
    t = np.linspace(0, 0.35, int(SR * 0.35), False)
    # Dual car horn: 370Hz and 466Hz with warm harmonics
    env = np.minimum(t / 0.02, 1.0) * np.exp(-1.5 * t)
    s = (np.sin(2 * np.pi * 370.0 * t) + 0.8 * np.sin(2 * np.pi * 466.0 * t) +
         0.3 * np.sin(2 * np.pi * 740.0 * t) + 0.2 * np.sin(2 * np.pi * 932.0 * t)) * env
    return s

def gen_fanfare():
    # Brass trumpet fanfare: C5 (523Hz), E5 (659Hz), G5 (784Hz), C6 (1046Hz)
    notes = [(523.25, 0.12), (659.25, 0.12), (783.99, 0.14), (1046.50, 0.45)]
    all_s = []
    for f, dur in notes:
        t = np.linspace(0, dur, int(SR * dur), False)
        env = np.minimum(t / 0.015, 1.0) * np.exp(-2.0 * t)
        # Brassy harmonics
        sig = (np.sin(2 * np.pi * f * t) +
               0.5 * np.sin(4 * np.pi * f * t) +
               0.25 * np.sin(6 * np.pi * f * t) +
               0.12 * np.sin(8 * np.pi * f * t)) * env
        all_s.append(sig)
    return np.concatenate(all_s)

def gen_drop():
    t = np.linspace(0, 0.15, int(SR * 0.15), False)
    freq = 600.0 + 1600.0 * (t / 0.15)**2
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.sin(np.pi * t / 0.15)**0.8 * np.exp(-15.0 * t)
    s = np.sin(phase) * env
    return s

def gen_siren():
    # 1.5s seamless ambulance siren sweep (for loop)
    dur = 1.5
    t = np.linspace(0, dur, int(SR * dur), False)
    freq = 750.0 + 350.0 * (0.5 + 0.5 * np.sin(2 * np.pi * (1.0 / dur) * t - np.pi/2))
    phase = 2 * np.pi * np.cumsum(freq) / SR
    # Square/saw hybrid with low pass warmth
    s = (np.sin(phase) + 0.35 * np.sin(3 * phase) + 0.15 * np.sin(5 * phase))
    return s

def gen_skid():
    t = np.linspace(0, 0.35, int(SR * 0.35), False)
    noise = np.random.uniform(-1, 1, len(t))
    # Screeching pitch sweep
    freq = 2200.0 * np.exp(-4.0 * t) + 400.0
    phase = 2 * np.pi * np.cumsum(freq) / SR
    screech = np.sin(phase) + 0.4 * np.sin(2.1 * phase)
    env = np.minimum(t / 0.03, 1.0) * np.exp(-3.5 * t)
    s = (0.7 * screech + 0.5 * noise) * env
    return s

# ----------------- Mode 8 Engines & SFX -----------------
def gen_engine():
    dur = 0.8
    t = np.linspace(0, dur, int(SR * dur), False)
    # Rhythmic V8 idle pulses at ~25 Hz
    pulses = np.zeros_like(t)
    pulse_rate = 26.0
    pulse_period = SR / pulse_rate
    for idx in range(0, len(t), int(pulse_period)):
        p_len = min(int(SR * 0.025), len(t) - idx)
        pt = np.linspace(0, 0.025, p_len, False)
        pulses[idx:idx + p_len] += (np.sin(2 * np.pi * 65.0 * pt) + 0.6 * np.sin(2 * np.pi * 130.0 * pt)) * np.exp(-120.0 * pt)
    noise = np.random.uniform(-0.15, 0.15, len(t))
    env = np.minimum(t / 0.05, 1.0) * np.exp(-1.0 * t)
    return (pulses + noise) * env

def gen_rocket():
    dur = 1.0
    t = np.linspace(0, dur, int(SR * dur), False)
    noise = np.random.uniform(-1, 1, len(t))
    freq = 90.0 + 600.0 * (t / dur)**1.8
    phase = 2 * np.pi * np.cumsum(freq) / SR
    rumble = np.sin(phase)
    env = np.minimum(t / 0.4, 1.0) * np.maximum(1.0 - (t - 0.7) / 0.3, 0.2)
    return (0.6 * rumble + 0.6 * noise) * env

# ----------------- Mode 8 Animals -----------------
def gen_animal_dog():
    # Double bark "Gâu! Gâu!"
    def single_bark():
        t = np.linspace(0, 0.18, int(SR * 0.18), False)
        freq = 360.0 * np.exp(-9.0 * t) + 120.0
        phase = 2 * np.pi * np.cumsum(freq) / SR
        env = np.minimum(t / 0.015, 1.0) * np.exp(-14.0 * t)
        noise = np.random.uniform(-0.3, 0.3, len(t))
        return (np.sin(phase) + 0.5 * np.sin(2 * phase) + noise) * env
    b1 = single_bark()
    gap = np.zeros(int(SR * 0.08))
    b2 = single_bark() * 0.9
    return np.concatenate([b1, gap, b2])

def gen_animal_cat():
    # "Meo meo" pitch sweep 550Hz -> 850Hz -> 420Hz
    t = np.linspace(0, 0.55, int(SR * 0.55), False)
    freq = np.piecewise(t, [t < 0.22, t >= 0.22],
                        [lambda x: 500.0 + 350.0 * (x / 0.22),
                         lambda x: 850.0 - 450.0 * ((x - 0.22) / 0.33)])
    phase = 2 * np.pi * np.cumsum(freq) / SR
    formant = np.sin(2 * np.pi * 1800.0 * t) * 0.25
    env = np.minimum(t / 0.06, 1.0) * np.exp(-2.5 * t)
    return (np.sin(phase) + 0.3 * np.sin(2 * phase) + formant) * env

def gen_animal_bird():
    # Melodic chirp trill
    all_c = []
    for _ in range(3):
        t = np.linspace(0, 0.08, int(SR * 0.08), False)
        freq = 2800.0 + 1400.0 * np.sin(np.pi * t / 0.08)
        phase = 2 * np.pi * np.cumsum(freq) / SR
        env = np.sin(np.pi * t / 0.08)
        all_c.append(np.sin(phase) * env)
        all_c.append(np.zeros(int(SR * 0.03)))
    return np.concatenate(all_c)

def gen_animal_cow():
    # Deep "Ùm bòoo"
    t = np.linspace(0, 0.75, int(SR * 0.75), False)
    freq = 175.0 - 30.0 * (t / 0.75)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.minimum(t / 0.1, 1.0) * np.exp(-1.8 * t)
    s = (np.sin(phase) + 0.6 * np.sin(2 * phase) + 0.3 * np.sin(3 * phase)) * env
    return s

def gen_animal_duck():
    # Nasal quack double "Cạp cạp"
    def quack():
        t = np.linspace(0, 0.16, int(SR * 0.16), False)
        freq = 380.0 * np.exp(-4.0 * t) + 160.0
        phase = 2 * np.pi * np.cumsum(freq) / SR
        nasal = np.sin(2 * np.pi * 1200.0 * t) * 0.4
        env = np.minimum(t / 0.02, 1.0) * np.exp(-11.0 * t)
        return (np.sign(np.sin(phase)) * 0.6 + nasal) * env
    q1 = quack()
    gap = np.zeros(int(SR * 0.07))
    q2 = quack() * 0.85
    return np.concatenate([q1, gap, q2])

def gen_animal_frog():
    # Croak "Ộp ộp"
    t = np.linspace(0, 0.28, int(SR * 0.28), False)
    tremolo = 0.5 + 0.5 * np.sin(2 * np.pi * 32.0 * t)
    freq = 190.0 - 50.0 * (t / 0.28)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.minimum(t / 0.02, 1.0) * np.exp(-6.0 * t)
    return (np.sin(phase) + 0.5 * np.sin(3 * phase)) * tremolo * env

def gen_animal_lion():
    # Ferocious roar
    t = np.linspace(0, 0.85, int(SR * 0.85), False)
    freq = 180.0 * np.exp(-2.0 * t) + 75.0 + 15.0 * np.sin(2 * np.pi * 20.0 * t)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    noise = np.random.uniform(-0.4, 0.4, len(t))
    env = np.minimum(t / 0.12, 1.0) * np.exp(-1.6 * t)
    return (np.sin(phase) + 0.7 * np.sin(2 * phase) + noise) * env

def gen_animal_pig():
    # Oink "Ụt ịt"
    t = np.linspace(0, 0.25, int(SR * 0.25), False)
    freq = 320.0 - 120.0 * (t / 0.25)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    grunt = np.sin(2 * np.pi * 950.0 * t) * 0.35
    env = np.minimum(t / 0.02, 1.0) * np.exp(-8.0 * t)
    return (np.sin(phase) + grunt) * env

def gen_animal_elephant():
    # Trumpet call
    t = np.linspace(0, 0.65, int(SR * 0.65), False)
    freq = 400.0 + 350.0 * (t / 0.65)**1.2
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.minimum(t / 0.05, 1.0) * np.exp(-2.0 * t)
    s = (np.sin(phase) + 0.5 * np.sin(2 * phase) + 0.3 * np.sin(4 * phase)) * env
    return s

def gen_animal_goat():
    # Bleat "Beee-eee"
    t = np.linspace(0, 0.55, int(SR * 0.55), False)
    tremolo = 0.5 + 0.5 * np.sin(2 * np.pi * 18.0 * t)
    freq = 320.0
    phase = 2 * np.pi * freq * t
    env = np.minimum(t / 0.04, 1.0) * np.exp(-3.0 * t)
    return (np.sin(phase) + 0.4 * np.sin(2 * phase)) * tremolo * env

def gen_animal_rooster():
    # "Ò ó o"
    notes = [(350.0, 0.12), (520.0, 0.15), (700.0, 0.45)]
    all_s = []
    for f, dur in notes:
        t = np.linspace(0, dur, int(SR * dur), False)
        env = np.minimum(t / 0.03, 1.0) * np.exp(-1.5 * t)
        sig = (np.sin(2 * np.pi * f * t) + 0.4 * np.sin(4 * np.pi * f * t)) * env
        all_s.append(sig)
    return np.concatenate(all_s)

def gen_animal_owl():
    # "Hú... huuu"
    t1 = np.linspace(0, 0.18, int(SR * 0.18), False)
    h1 = np.sin(2 * np.pi * 350.0 * t1) * np.sin(np.pi * t1 / 0.18)
    gap = np.zeros(int(SR * 0.08))
    t2 = np.linspace(0, 0.35, int(SR * 0.35), False)
    h2 = np.sin(2 * np.pi * 320.0 * t2) * np.sin(np.pi * t2 / 0.35)
    return np.concatenate([h1, gap, h2])

def gen_animal_wolf():
    # Howl
    t = np.linspace(0, 0.9, int(SR * 0.9), False)
    freq = np.piecewise(t, [t < 0.35, t >= 0.35],
                        [lambda x: 400.0 + 350.0 * (x / 0.35),
                         lambda x: 750.0 - 250.0 * ((x - 0.35) / 0.55)])
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.minimum(t / 0.1, 1.0) * np.exp(-1.5 * t)
    return np.sin(phase) * env

def gen_animal_tiger():
    # Menacing tiger growl
    t = np.linspace(0, 0.7, int(SR * 0.7), False)
    freq = 140.0 + 30.0 * np.sin(2 * np.pi * 15.0 * t)
    phase = 2 * np.pi * np.cumsum(freq) / SR
    noise = np.random.uniform(-0.35, 0.35, len(t))
    env = np.minimum(t / 0.08, 1.0) * np.exp(-2.0 * t)
    return (np.sin(phase) + 0.6 * np.sin(2 * phase) + noise) * env

def gen_animal_horse():
    # Whinny / neigh
    t = np.linspace(0, 0.6, int(SR * 0.6), False)
    vibrato = 120.0 * np.sin(2 * np.pi * 22.0 * t)
    freq = 1100.0 * np.exp(-2.5 * t) + 400.0 + vibrato
    phase = 2 * np.pi * np.cumsum(freq) / SR
    env = np.minimum(t / 0.04, 1.0) * np.exp(-3.0 * t)
    return np.sin(phase) * env

def gen_animal_sheep():
    # Sheep "Baaa"
    t = np.linspace(0, 0.5, int(SR * 0.5), False)
    tremolo = 0.5 + 0.5 * np.sin(2 * np.pi * 14.0 * t)
    freq = 260.0
    phase = 2 * np.pi * freq * t
    env = np.minimum(t / 0.05, 1.0) * np.exp(-2.5 * t)
    return (np.sin(phase) + 0.5 * np.sin(2 * phase)) * tremolo * env

def main():
    # Mode 6 missing
    save_wav("audio_assets/mode6/boing.wav", gen_boing())
    save_wav("audio_assets/mode6/laser.wav", gen_laser())
    save_wav("audio_assets/mode6/horn.wav", gen_horn())
    save_wav("audio_assets/mode6/fanfare.wav", gen_fanfare())
    save_wav("audio_assets/mode6/drop.wav", gen_drop())
    save_wav("audio_assets/mode6/siren.wav", gen_siren())
    save_wav("audio_assets/mode6/skid.wav", gen_skid())

    # Mode 8 vehicles
    save_wav("audio_assets/mode8/engine.wav", gen_engine())
    save_wav("audio_assets/mode8/rocket.wav", gen_rocket())

    # Mode 8 animals
    save_wav("audio_assets/mode8/animal_dog.wav", gen_animal_dog())
    save_wav("audio_assets/mode8/animal_cat.wav", gen_animal_cat())
    save_wav("audio_assets/mode8/animal_bird.wav", gen_animal_bird())
    save_wav("audio_assets/mode8/animal_cow.wav", gen_animal_cow())
    save_wav("audio_assets/mode8/animal_duck.wav", gen_animal_duck())
    save_wav("audio_assets/mode8/animal_frog.wav", gen_animal_frog())
    save_wav("audio_assets/mode8/animal_lion.wav", gen_animal_lion())
    save_wav("audio_assets/mode8/animal_pig.wav", gen_animal_pig())
    save_wav("audio_assets/mode8/animal_elephant.wav", gen_animal_elephant())
    save_wav("audio_assets/mode8/animal_goat.wav", gen_animal_goat())
    save_wav("audio_assets/mode8/animal_rooster.wav", gen_animal_rooster())
    save_wav("audio_assets/mode8/animal_owl.wav", gen_animal_owl())
    save_wav("audio_assets/mode8/animal_wolf.wav", gen_animal_wolf())
    save_wav("audio_assets/mode8/animal_tiger.wav", gen_animal_tiger())
    save_wav("audio_assets/mode8/animal_horse.wav", gen_animal_horse())
    save_wav("audio_assets/mode8/animal_sheep.wav", gen_animal_sheep())

    print("\nALL AUDIO ASSETS SUCCESSFULLY GENERATED!")

if __name__ == '__main__':
    main()
