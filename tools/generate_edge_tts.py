import os
import sys
import asyncio
import numpy as np
import soundfile as sf
import edge_tts

sys.stdout.reconfigure(encoding='utf-8')

VOICE_PRIMARY = "vi-VN-HoaiMyNeural"
VOICE_BACKUP  = "vi-VN-NamMinhNeural"
OUT_DIR = "audio_assets/mode9"
TARGET_SR = 24000

os.makedirs(OUT_DIR, exist_ok=True)

letter_map = {
    'a': 'Chữ A.',
    'b': 'Chữ B.',
    'c': 'Chữ C.',
    'd': 'Chữ D.',
    'e': 'Chữ E.',
    'f': 'Chữ F.',
    'g': 'Chữ G.',
    'h': 'Chữ H.',
    'i': 'Chữ I.',
    'j': 'Chữ J.',
    'k': 'Chữ K.',
    'l': 'Chữ L.',
    'm': 'Chữ M.',
    'n': 'Chữ N.',
    'o': 'Chữ O.',
    'p': 'Chữ P.',
    'q': 'Chữ Q.',
    'r': 'Chữ R.',
    's': 'Chữ S.',
    't': 'Chữ T.',
    'u': 'Chữ U.',
    'v': 'Chữ V.',
    'w': 'Chữ W.',
    'x': 'Chữ X.',
    'y': 'Chữ Y.',
    'z': 'Chữ Z.'
}

number_map = {
    "num_0": "Số 0.",
    "num_1": "Số 1.",
    "num_2": "Số 2.",
    "num_3": "Số 3.",
    "num_4": "Số 4.",
    "num_5": "Số 5.",
    "num_6": "Số 6.",
    "num_7": "Số 7.",
    "num_8": "Số 8.",
    "num_9": "Số 9.",
    "num_10": "Số 10."
}

special_keys_map = {
    "key_space": "Phím Cách.",
    "key_enter": "Phím En-tơ.",
    "key_backspace": "Phím Xóa.",
    "key_tab": "Phím Táp.",
    "key_capslock": "Phím Viết Hoa.",
    "key_shift": "Phím Shift.",
    "key_ctrl": "Phím Côn-trôn.",
    "key_alt": "Phím An.",
    "key_win": "Phím Windows.",
    "key_esc": "Phím Thoát.",
    "key_delete": "Phím Xóa Phải.",
    "key_insert": "Phím Chèn.",
    "key_home": "Về Đầu Dòng.",
    "key_end": "Về Cuối Dòng.",
    "key_pageup": "Trang Lên.",
    "key_pagedown": "Trang Xuống.",
    "key_arrow_up": "Mũi Tên Lên.",
    "key_arrow_down": "Mũi Tên Xuống.",
    "key_arrow_left": "Mũi Tên Trái.",
    "key_arrow_right": "Mũi Tên Phải.",
    "key_numlock": "Phím Khóa Số.",
    "key_scrolllock": "Phím Cuộn Trang.",
    "key_pause": "Phím Tạm Dừng.",
    "key_prtscr": "Chụp Màn Hình.",
    "key_menu": "Phím Trình Đơn.",
    "key_f1": "Phím F-1.",
    "key_f2": "Phím F-2.",
    "key_f3": "Phím F-3.",
    "key_f4": "Phím F-4.",
    "key_f5": "Phím F-5.",
    "key_f6": "Phím F-6.",
    "key_f7": "Phím F-7.",
    "key_f8": "Phím F-8.",
    "key_f9": "Phím F-9.",
    "key_f10": "Phím F 10.",
    "key_f11": "Phím F 11.",
    "key_f12": "Phím F 12."
}

symbols_map = {
    "sym_plus": "Dấu Cộng.",
    "sym_minus": "Dấu Trừ.",
    "sym_multiply": "Dấu Nhân.",
    "sym_divide": "Dấu Chia.",
    "sym_equal": "Dấu Bằng.",
    "sym_dot": "Dấu Chấm.",
    "sym_comma": "Dấu Phẩy.",
    "sym_semicolon": "Dấu Chấm Phẩy.",
    "sym_colon": "Dấu Hai Chấm.",
    "sym_exclaim": "Dấu Chấm Than.",
    "sym_question": "Dấu Hỏi.",
    "sym_quote_single": "Dấu Nháy Đơn.",
    "sym_quote_double": "Dấu Nháy Kép.",
    "sym_at": "A Còng.",
    "sym_hash": "Dấu Thăng.",
    "sym_dollar": "Dấu Đô La.",
    "sym_percent": "Phần Trăm.",
    "sym_caret": "Dấu Mũ.",
    "sym_ampersand": "Dấu Và.",
    "sym_paren_open": "Mở Ngoặc Đơn.",
    "sym_paren_close": "Đóng Ngoặc Đơn.",
    "sym_bracket_open": "Mở Ngoặc Vuông.",
    "sym_bracket_close": "Đóng Ngoặc Vuông.",
    "sym_brace_open": "Mở Ngoặc Nhọn.",
    "sym_brace_close": "Đóng Ngoặc Nhọn.",
    "sym_less_than": "Dấu Nhỏ Hơn.",
    "sym_greater_than": "Dấu Lớn Hơn.",
    "sym_backslash": "Gạch Chéo Ngược.",
    "sym_pipe": "Gạch Đứng.",
    "sym_tilde": "Dấu Ngã.",
    "sym_backtick": "Dấu Huyền.",
    "sym_underscore": "Gạch Dưới."
}

praise_text = "Hoan hô! Bé giỏi quá!"

async def synthesize_one(text, dest_wav):
    if os.path.exists(dest_wav) and os.path.getsize(dest_wav) > 1000:
        return  # Đã có file

    temp_mp3 = dest_wav.replace(".wav", f"_temp_{os.getpid()}.mp3")
    success = False
    
    for attempt in range(6):
        voice = VOICE_PRIMARY if attempt < 3 else VOICE_BACKUP
        try:
            communicate = edge_tts.Communicate(text, voice)
            await communicate.save(temp_mp3)
            if os.path.exists(temp_mp3) and os.path.getsize(temp_mp3) > 100:
                success = True
                break
        except Exception as e:
            wait_time = 1.2 + attempt * 0.8
            if attempt >= 2:
                print(f" [Thử lại {attempt+1}/6 sau {wait_time:.1f}s cho '{text}']: {e}")
            await asyncio.sleep(wait_time)
            
    if not success or not os.path.exists(temp_mp3):
        raise RuntimeError(f"Không thể tạo âm thanh cho: {text}")

    # Đọc audio
    data, sr = sf.read(temp_mp3)
    if os.path.exists(temp_mp3):
        try:
            os.remove(temp_mp3)
        except:
            pass
        
    if data.ndim > 1:
        data = np.mean(data, axis=1)
        
    # Cắt khoảng lặng đầu
    thresh = 0.015
    start_idx = 0
    for idx, val in enumerate(data):
        if abs(val) > thresh:
            start_idx = max(0, idx - int(sr * 0.010))  # 10ms padding
            break
    data = data[start_idx:]

    # Cắt khoảng lặng đuôi
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
    sys.stdout.flush()

async def main():
    print(f"============================================================")
    print(f"  TẠO ÂM THANH TIẾNG VIỆT MICROSOFT EDGE TTS CHO TOÀN BỘ BÀN PHÍM")
    print(f"============================================================")
    sys.stdout.flush()

    # 1. Letters
    print("\n>>> Kiểm tra 26 chữ cái tiếng Việt...")
    sys.stdout.flush()
    for char, text in letter_map.items():
        dest = os.path.join(OUT_DIR, f"letter_{char}.wav")
        await synthesize_one(text, dest)

    # 2. Numbers
    print("\n>>> Kiểm tra 11 số đếm tiếng Việt (0..10)...")
    sys.stdout.flush()
    for name, text in number_map.items():
        dest = os.path.join(OUT_DIR, f"{name}.wav")
        await synthesize_one(text, dest)

    # 3. Special Keys
    print("\n>>> Tạo âm thanh các phím chức năng & điều hướng full-size...")
    sys.stdout.flush()
    for name, text in special_keys_map.items():
        dest = os.path.join(OUT_DIR, f"{name}.wav")
        await synthesize_one(text, dest)
        await asyncio.sleep(0.5)

    # 4. Symbols & Punctuation
    print("\n>>> Tạo âm thanh các ký tự, dấu câu & phép toán full-size...")
    sys.stdout.flush()
    for name, text in symbols_map.items():
        dest = os.path.join(OUT_DIR, f"{name}.wav")
        await synthesize_one(text, dest)
        await asyncio.sleep(0.5)

    # 5. Praise
    print("\n>>> Kiểm tra câu khen ngợi...")
    sys.stdout.flush()
    dest = os.path.join(OUT_DIR, "praise.wav")
    await synthesize_one(praise_text, dest)

    print("\n>>> HOÀN TẤT TẠO TOÀN BỘ KHO ÂM THANH BÀN PHÍM FULL-SIZE!")
    sys.stdout.flush()

if __name__ == '__main__':
    asyncio.run(main())
