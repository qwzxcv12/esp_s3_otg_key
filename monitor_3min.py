import sys
import time
import serial

if sys.stdout.encoding != 'utf-8':
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

PORT = "COM4"
BAUD = 115200
DURATION_SEC = 220  # 3 phút 40 giây

print("=" * 65, flush=True)
print(f"   BẮT ĐẦU THEO DÕI LOG REALTIME TRÊN {PORT} ({DURATION_SEC} GIÂY)", flush=True)
print("   - Sau 60s: Thiết bị sẽ tự động DIM (giảm độ sáng LED xuống 20%)", flush=True)
print("   - Sau 180s (3p): Thiết bị sẽ phát âm tạm biệt và vào SLEEP", flush=True)
print("   - Khi bạn bấm phím: Hệ thống sẽ lập tức thức dậy (WAKE UP)!", flush=True)
print("=" * 65, flush=True)

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    ser.setDTR(False)
    ser.setRTS(False)
except Exception as e:
    print(f"[ERROR] Không thể kết nối {PORT}: {e}", flush=True)
    sys.exit(1)

start_time = time.time()
last_heartbeat = 0
buf = ""

try:
    while (time.time() - start_time) < DURATION_SEC:
        now = time.time()
        elapsed = int(now - start_time)

        # Heartbeat log mỗi 30s nếu không có dữ liệu
        if elapsed > 0 and elapsed % 30 == 0 and elapsed != last_heartbeat:
            last_heartbeat = elapsed
            print(f"[{elapsed:03d}s] ... [ĐANG ĐẾM THỜI GIAN RẢNH: {elapsed}s / 180s] ...", flush=True)

        raw = ser.read(ser.in_waiting or 1)
        if raw:
            text = raw.decode("utf-8", errors="replace")
            buf += text
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip("\r")
                if line:
                    print(f"[{elapsed:03d}s] {line}", flush=True)
        time.sleep(0.01)

    print("\n" + "=" * 65, flush=True)
    print(f"   HOÀN THÀNH {DURATION_SEC} GIÂY THEO DÕI LOG!", flush=True)
    print("=" * 65, flush=True)
except KeyboardInterrupt:
    print("\n[DỪNG THEO DÕI BỞI NGƯỜI DÙNG]", flush=True)
finally:
    ser.close()
