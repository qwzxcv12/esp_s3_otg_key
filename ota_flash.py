#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-S3 High-Speed Wireless OTA Uploader
Flash firmware over Wi-Fi via HTTP POST with automatic UDP discovery and live progress bar.
"""

import os
import sys
import json
import time
import socket
import argparse
import http.client
import urllib.request
import urllib.error

CONFIG_FILE = "builder_config.json"
UDP_DISCOVERY_PORT = 45454
DEFAULT_MDNS_HOST = "esp32-toy.local"
HTTP_PORT = 80

def load_config():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {}

def save_config(cfg):
    try:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(cfg, f, indent=4)
    except Exception as e:
        print(f"[WARN] Khong the luu config: {e}")

def discover_esp32_udp(timeout=1.5):
    """Broadcast UDP ping to find ESP32 instantly on local Wi-Fi without needing mDNS"""
    print(f"[DISCOVERY] Dang quet tim ESP32 qua UDP broadcast (cong {UDP_DISCOVERY_PORT})...")
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.settimeout(timeout)
    
    try:
        message = b"DISCOVER_ESP32_OTA"
        # Broadcast to local subnet
        s.sendto(message, ("255.255.255.255", UDP_DISCOVERY_PORT))
        
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                data, addr = s.recvfrom(512)
                text = data.decode("utf-8", errors="ignore").strip()
                # Expected format: ESP32_OTA_ACK:esp32-toy:192.168.1.xxx
                if text.startswith("ESP32_OTA_ACK"):
                    parts = text.split(":")
                    ip = addr[0]
                    hostname = parts[1] if len(parts) > 1 else "esp32-toy"
                    if len(parts) > 2 and parts[2]:
                        ip = parts[2]
                    print(f"            -> Tim thay [{hostname}] tai dia chi IP: {ip}")
                    return ip
            except socket.timeout:
                break
    except Exception as e:
        pass
    finally:
        s.close()
    return None

def check_esp32_status(host, timeout=3.0, retries=2):
    """Check /status endpoint on ESP32 with retries"""
    url = f"http://{host}:{HTTP_PORT}/status"
    for attempt in range(retries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "OTA-Uploader"})
            with urllib.request.urlopen(req, timeout=timeout) as response:
                if response.status == 200:
                    data = json.loads(response.read().decode("utf-8"))
                    return data
        except Exception:
            if attempt < retries - 1:
                time.sleep(0.3)
    return None

def resolve_target(specified_target=None):
    """Find target IP using UDP discovery, cached IP, or mDNS"""
    config = load_config()

    if specified_target:
        return specified_target

    # 1. Try instant UDP discovery
    discovered_ip = discover_esp32_udp(timeout=1.5)
    if discovered_ip:
        config["last_esp32_ip"] = discovered_ip
        save_config(config)
        return discovered_ip

    # 2. Try cached IP in builder_config.json
    cached_ip = config.get("last_esp32_ip", "192.168.1.156")
    if cached_ip:
        print(f"[DISCOVERY] Kiem tra IP da luu truoc do: {cached_ip}...")
        status = check_esp32_status(cached_ip, timeout=3.0, retries=2)
        if status:
            print(f"            -> Ket noi thanh cong toi {cached_ip}! (Phien ban: {status.get('version', '?')})")
            return cached_ip

    # 3. Try mDNS hostname
    print(f"[DISCOVERY] Thu ket noi qua mDNS ({DEFAULT_MDNS_HOST})...")
    status = check_esp32_status(DEFAULT_MDNS_HOST, timeout=3.0, retries=2)
    if status:
        print(f"            -> Ket noi thanh cong qua {DEFAULT_MDNS_HOST}!")
        return DEFAULT_MDNS_HOST

    return None

def find_binary(specified_bin=None):
    """Locate the firmware .bin file"""
    if specified_bin and os.path.exists(specified_bin):
        return specified_bin
    
    config = load_config()
    project_name = config.get("project_name", "esp_s3_mp3_lcd")
    candidate = f"{project_name}.bin"
    if os.path.exists(candidate):
        return candidate

    # Search in current directory and build/
    for root, dirs, files in os.walk("."):
        if f"{project_name}.bin" in files:
            return os.path.join(root, f"{project_name}.bin")
        if candidate in files:
            return os.path.join(root, candidate)

    return None

def flash_ota(target_host, bin_path):
    """Stream firmware file to ESP32 /update endpoint via HTTP POST"""
    file_size = os.path.getsize(bin_path)
    file_size_kb = file_size / 1024.0

    print("----------------------------------------------------------")
    print(f"  Firmware File : {os.path.basename(bin_path)} ({file_size_kb:.1f} KB)")
    print(f"  Target Host   : {target_host}:{HTTP_PORT}")
    print("----------------------------------------------------------")

    # Fetch status before upload
    status = check_esp32_status(target_host, timeout=3.0)
    if status:
        print(f"[DEVICE] Phien ban hien tai: {status.get('version', 'N/A')} | Partition: {status.get('running_partition', 'N/A')} | Free RAM: {status.get('free_heap', 0)//1024} KB")

    print(f"\n[OTA FLASH] Dang truyen du lieu khong day...")
    conn = http.client.HTTPConnection(target_host, HTTP_PORT, timeout=240)
    
    try:
        conn.putrequest("POST", "/update")
        conn.putheader("Content-Type", "application/octet-stream")
        conn.putheader("Content-Length", str(file_size))
        conn.putheader("Host", target_host)
        conn.endheaders()

        chunk_size = 4096  # 4KB chunk matching optimal ESP32 buffer
        sent_bytes = 0
        start_time = time.time()
        bar_len = 30

        with open(bin_path, "rb") as f:
            while True:
                chunk = f.read(chunk_size)
                if not chunk:
                    break
                conn.send(chunk)
                sent_bytes += len(chunk)
                
                # Live Progress Bar
                elapsed = time.time() - start_time
                pct = (sent_bytes / file_size) * 100.0
                speed_kb = (sent_bytes / 1024.0) / elapsed if elapsed > 0 else 0
                speed_mb = speed_kb / 1024.0
                eta = (file_size - sent_bytes) / (sent_bytes / elapsed) if (elapsed > 0 and sent_bytes > 0) else 0
                
                filled = int(bar_len * (sent_bytes / file_size))
                bar = "█" * filled + "░" * (bar_len - filled)
                sys.stdout.write(f"\r  [{bar}] {pct:5.1f}%  ({sent_bytes/1024:.1f}/{file_size_kb:.1f} KB) | {speed_mb:4.2f} MB/s | ETA: {eta:3.1f}s")
                sys.stdout.flush()

        print("\n\n[FLASH] Hoan tat truyen file! Dang doi ESP32 ghi Flash & khoi dong lai...")
        success = False
        try:
            response = conn.getresponse()
            if response.status == 200:
                success = True
        except Exception:
            # ESP32 can reboot immediately before Windows TCP finishes reading HTTP response
            pass

        total_time = time.time() - start_time
        avg_speed_kb = file_size_kb / total_time if total_time > 0 else 0

        # Verify device reboot and partition switch
        print("[VERIFY] Dang xac thuc trang thai boot cua ESP32...")
        time.sleep(3.0)
        new_status = None
        for _ in range(8):
            new_status = check_esp32_status(target_host, timeout=3.0, retries=1)
            if new_status:
                break
            time.sleep(1.0)

        old_part = status.get("running_partition", "") if status else ""
        new_part = new_status.get("running_partition", "") if new_status else ""

        if success or (new_status and (new_part != old_part or not old_part)):
            print("==========================================================")
            print("  >>> SUCCESS! NAP FIRMWARE KHONG DAY THANH CONG!")
            print(f"  >>> Thoi gian nap: {total_time:.2f} giay (Toc do TB: {avg_speed_kb:.1f} KB/s)")
            if new_status:
                print(f"  >>> ESP32-S3 da chuyen boot partition: [{old_part}] -> [{new_part}]")
                print(f"  >>> IP Address: {new_status.get('ip')} | RAM Free: {new_status.get('free_heap', 0)//1024} KB")
            print("==========================================================")
            return True
        else:
            print("\n[ERROR] Khong the xac thuc ESP32 khoi dong lai vao partition moi.")
            return False

    except Exception as e:
        print(f"\n[ERROR] Loi truyen file OTA: {e}")
        return False
    finally:
        conn.close()

def main():
    parser = argparse.ArgumentParser(description="ESP32-S3 High-Speed Wireless OTA Uploader")
    parser.add_argument("--bin", type=str, help="Duong dan toi file firmware .bin")
    parser.add_argument("--target", type=str, help="Dia chi IP hoac Hostname cua ESP32")
    args = parser.parse_args()

    print("==========================================================")
    print("      ESP32-S3 High-Speed Wireless OTA Uploader           ")
    print("==========================================================")

    bin_path = find_binary(args.bin)
    if not bin_path or not os.path.exists(bin_path):
        print("[ERROR] Khong tim thay file firmware .bin! Hay bien dich truoc khi nap.")
        sys.exit(1)

    target_host = resolve_target(args.target)
    if not target_host:
        print("\n[ERROR] Khong the tim thay ESP32 tren mang Wi-Fi 'HG156'!")
        print("Huong dan xu ly:")
        print("  1. Dam bao ESP32 da duoc cap nguon va nam trong pham vi phu song Wi-Fi.")
        print("  2. Dam bao may tinh va ESP32 dang ket noi vao CUNG MOT mang Wi-Fi.")
        print("  3. Neu day la lan dau tien nap OTA, can nap qua cap COM mot lan de ghi phan vung moi.")
        print("  4. Hoac ban co the chi dinh truc tiep IP: python ota_flash.py --target 192.168.1.xxx")
        sys.exit(1)

    success = flash_ota(target_host, bin_path)
    if not success:
        sys.exit(1)

if __name__ == "__main__":
    main()
