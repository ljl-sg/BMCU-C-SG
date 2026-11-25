#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BambuBus 抓包采集

从 **BMCU-BusSentry-SG 总线哨兵** 的 TCP 81 端口拉取原始字节流，落盘为：
  <前缀>.bin  原始字节
  <前缀>.csv  每块的偏移与时间戳（便于按时间定位）

用法:
    python capture.py <哨兵IP> [端口] [输出前缀]
例:
    python capture.py 192.168.1.50
    python capture.py 192.168.1.50 81 my_capture

结束按 Ctrl+C，然后可用 bambu_parse.py 解析：
    python bambu_parse.py my_capture.bin --stats --topo
"""
import socket
import sys
import time

if len(sys.argv) < 2:
    print(__doc__)
    sys.exit(1)

host = sys.argv[1]
port = int(sys.argv[2]) if len(sys.argv) > 2 else 81
prefix = sys.argv[3] if len(sys.argv) > 3 else time.strftime("capture_%Y%m%d_%H%M%S")

bin_path = prefix + ".bin"
csv_path = prefix + ".csv"

print(f"连接 {host}:{port} ...")
try:
    s = socket.create_connection((host, port), timeout=10)
except OSError as e:
    sys.exit(f"连接失败：{e}\n检查：哨兵 IP 是否正确、网页里「数据分析」是否已打开、是否同一局域网")
s.settimeout(1.0)
print(f"已连接，数据写入 {bin_path} / {csv_path}")
print("按 Ctrl+C 结束\n")

total = 0
chunks = 0
t0 = time.time()
last_report = t0

with open(bin_path, "wb") as fb, open(csv_path, "w", encoding="utf-8") as fc:
    fc.write("offset,timestamp,length\n")
    try:
        while True:
            try:
                data = s.recv(4096)
            except socket.timeout:
                data = b""
            if not data:
                now = time.time()
                if now - last_report >= 2.0:
                    rate = total / max(now - t0, 0.01)
                    print(f"  {total} 字节 / {chunks} 块 / {rate/1024:.1f} KB/s")
                    last_report = now
                if now - last_report >= 10:
                    break
                continue
            ts = time.time() - t0
            fb.write(data)
            fb.flush()
            fc.write(f"{total},{ts:.6f},{len(data)}\n")
            fc.flush()
            total += len(data)
            chunks += 1
    except KeyboardInterrupt:
        pass

print(f"\n结束：{total} 字节，{chunks} 块，耗时 {time.time()-t0:.1f}s")
print(f"  原始数据: {bin_path}")
print(f"  时间戳:   {csv_path}")
