import serial
import time
import sys

sys.stdout.reconfigure(encoding='utf-8')

DURATION = 30  # seconds
PORT = 'COM4'
BAUD = 115200

print(f"================================================================")
print(f"   BẮT ĐẦU THEO DÕI SERIAL COM4 TRONG {DURATION} GIÂY...")
print(f"   -> Hãy cắm bàn phím, gõ thử các phím, vặn núm xoay, nhấn SW...")
print(f"================================================================", flush=True)

try:
    ser = serial.Serial(PORT, BAUD, timeout=0.1)
except Exception as e:
    print(f"[ERROR] Không thể mở cổng {PORT}: {e}", flush=True)
    sys.exit(1)

# Clear any pending input
ser.reset_input_buffer()

start_time = time.time()
captured_lines = []

while time.time() - start_time < DURATION:
    try:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='replace')
            if line:
                sys.stdout.write(line)
                sys.stdout.flush()
                captured_lines.append(line)
        else:
            time.sleep(0.02)
    except Exception as e:
        print(f"[ERROR khi đọc serial]: {e}", flush=True)
        break

ser.close()

with open('tools/test_session_log.txt', 'w', encoding='utf-8') as f:
    f.writelines(captured_lines)

print(f"\n================================================================")
print(f"   KẾT THÚC THEO DÕI. ĐÃ GHI NHẬN {len(captured_lines)} DÒNG LOG.")
print(f"================================================================", flush=True)
