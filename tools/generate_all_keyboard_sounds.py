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

all_items = {
    # 1. Letters (already exist, will be skipped)
    "letter_a": "Chữ A", "letter_b": "Chữ B", "letter_c": "Chữ C", "letter_d": "Chữ D",
    "letter_e": "Chữ E", "letter_f": "Chữ F", "letter_g": "Chữ G", "letter_h": "Chữ H",
    "letter_i": "Chữ I", "letter_j": "Chữ J", "letter_k": "Chữ K", "letter_l": "Chữ L",
    "letter_m": "Chữ M", "letter_n": "Chữ N", "letter_o": "Chữ O", "letter_p": "Chữ P",
    "letter_q": "Chữ Q", "letter_r": "Chữ R", "letter_s": "Chữ S", "letter_t": "Chữ T",
    "letter_u": "Chữ U", "letter_v": "Chữ V", "letter_w": "Chữ W", "letter_x": "Chữ X",
    "letter_y": "Chữ Y", "letter_z": "Chữ Z",

    # 2. Numbers (already exist, will be skipped)
    "num_0": "Số 0", "num_1": "Số 1", "num_2": "Số 2", "num_3": "Số 3",
    "num_4": "Số 4", "num_5": "Số 5", "num_6": "Số 6", "num_7": "Số 7",
    "num_8": "Số 8", "num_9": "Số 9", "num_10": "Số 10",

    # 3. Special & Navigation Keys
    "key_space": "Phím Cách",
    "key_enter": "Phím En-tơ",
    "key_backspace": "Phím Xóa",
    "key_tab": "Phím Táp",
    "key_capslock": "Phím Viết Hoa",
    "key_shift": "Phím Shift",
    "key_ctrl": "Phím Côn-trôn",
    "key_alt": "Phím An",
    "key_win": "Phím Windows",
    "key_esc": "Phím Thoát",
    "key_delete": "Phím Xóa Phải",
    "key_insert": "Phím Chèn",
    "key_home": "Về Đầu Dòng",
    "key_end": "Về Cuối Dòng",
    "key_pageup": "Trang Lên",
    "key_pagedown": "Trang Xuống",
    "key_arrow_up": "Mũi Tên Lên",
    "key_arrow_down": "Mũi Tên Xuống",
    "key_arrow_left": "Mũi Tên Trái",
    "key_arrow_right": "Mũi Tên Phải",
    "key_numlock": "Phím Khóa Số",
    "key_scrolllock": "Khóa cuộn",
    "key_pause": "Tạm dừng",
    "key_prtscr": "Chụp ảnh",
    "key_menu": "Me-nu",
    "key_f1": "Phím F-1",
    "key_f2": "Phím F-2",
    "key_f3": "Phím F-3",
    "key_f4": "Phím F-4",
    "key_f5": "Phím F-5",
    "key_f6": "Phím F-6",
    "key_f7": "Phím F-7",
    "key_f8": "Phím F-8",
    "key_f9": "Phím F-9",
    "key_f10": "Phím F 10.",
    "key_f11": "Phím F 11.",
    "key_f12": "Phím F 12.",

    # 4. Symbols & Punctuation
    "sym_plus": "Dấu cộng",
    "sym_minus": "Dấu trừ",
    "sym_multiply": "Dấu nhân",
    "sym_divide": "Ký hiệu chia",
    "sym_equal": "Dấu bằng",
    "sym_dot": "Dấu chấm",
    "sym_comma": "Dấu phẩy",
    "sym_semicolon": "Dấu chấm phẩy",
    "sym_colon": "Dấu hai chấm",
    "sym_exclaim": "Dấu chấm than",
    "sym_question": "Dấu hỏi",
    "sym_quote_single": "Dấu nháy",
    "sym_quote_double": "Hai nháy",
    "sym_at": "A còng",
    "sym_hash": "Dấu thăng",
    "sym_dollar": "Dấu đô la",
    "sym_percent": "Phần trăm",
    "sym_caret": "Dấu mũ",
    "sym_ampersand": "Dấu và",
    "sym_paren_open": "Mở ngoặc",
    "sym_paren_close": "Đóng ngoặc",
    "sym_bracket_open": "Mở ngoặc vuông",
    "sym_bracket_close": "Khóa ngoặc vuông",
    "sym_brace_open": "Mở ngoặc nhọn",
    "sym_brace_close": "Đóng ngoặc nhọn",
    "sym_less_than": "Ký hiệu nhỏ",
    "sym_greater_than": "Dấu lớn",
    "sym_backslash": "Gạch lùi",
    "sym_pipe": "Gạch đứng",
    "sym_tilde": "Dấu ngã",
    "sym_backtick": "Dấu huyền",
    "sym_underscore": "Gạch dưới",

    # 5. Praise
    "praise": "Hoan hô! Bé giỏi quá!"
}

async def synthesize_one(name, text, dest_wav):
    if os.path.exists(dest_wav) and os.path.getsize(dest_wav) > 1000:
        return

    temp_mp3 = dest_wav.replace(".wav", f"_temp_{os.getpid()}.mp3")
    success = False

    for attempt in range(5):
        try:
            communicate = edge_tts.Communicate(text, VOICE)
            await communicate.save(temp_mp3)
            if os.path.exists(temp_mp3) and os.path.getsize(temp_mp3) > 100:
                success = True
                break
        except Exception as e:
            wait = 1.0 + attempt * 0.8
            await asyncio.sleep(wait)

    if not success or not os.path.exists(temp_mp3):
        print(f"FAILED to synthesize: {name} ('{text}')")
        return

    data, sr = sf.read(temp_mp3)
    if os.path.exists(temp_mp3):
        try:
            os.remove(temp_mp3)
        except:
            pass

    if data.ndim > 1:
        data = np.mean(data, axis=1)

    thresh = 0.015
    start_idx = 0
    for idx, val in enumerate(data):
        if abs(val) > thresh:
            start_idx = max(0, idx - int(sr * 0.010))
            break
    data = data[start_idx:]

    end_idx = len(data)
    for idx in range(len(data) - 1, -1, -1):
        if abs(data[idx]) > thresh:
            end_idx = min(len(data), idx + int(sr * 0.020))
            break
    data = data[:end_idx]

    pk = np.max(np.abs(data))
    if pk > 0.001:
        data = data * (0.95 / pk)

    fi = min(len(data), int(sr * 0.005))
    if fi > 0:
        data[:fi] *= np.linspace(0, 1, fi)
    fo = min(len(data), int(sr * 0.015))
    if fo > 0:
        data[-fo:] *= np.linspace(1, 0, fo)

    pcm16 = (np.clip(data, -1.0, 1.0) * 32767.0).astype(np.int16)
    sf.write(dest_wav, pcm16, sr, subtype="PCM_16")
    dur = len(pcm16) / sr
    print(f" -> {os.path.basename(dest_wav)}: '{text}' ({dur:.2f}s, {len(pcm16)*2/1024:.1f} KB)", flush=True)

async def main():
    print(f"============================================================")
    print(f"  TẠO ĐẦY ĐỦ TOÀN BỘ ÂM THANH BÀN PHÍM FULL-SIZE (EDGE TTS)")
    print(f"============================================================")
    
    total = len(all_items)
    print(f"Tổng số phím & ký tự cần kiểm tra: {total}")
    
    for i, (name, text) in enumerate(all_items.items(), 1):
        dest = os.path.join(OUT_DIR, f"{name}.wav")
        if not os.path.exists(dest) or os.path.getsize(dest) < 1000:
            print(f"[{i}/{total}] Đang tạo {name}: '{text}'...", flush=True)
            await synthesize_one(name, text, dest)
            await asyncio.sleep(0.4)
            
    print("\n>>> HOÀN TẤT 100% TOÀN BỘ BỘ ÂM THANH BÀN PHÍM FULL-SIZE!", flush=True)

if __name__ == '__main__':
    asyncio.run(main())
