#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""只编译自己要用的固件（不走 1020 全量）

默认矩阵（按你自己两台机器）：
    A1   : standard(A1)     回抽 0.20  槽位 AUTO/A/B/C/D
    P2S  : high_force_load  回抽 0.90  槽位 AUTO/A/B/C/D
    其余 : AUTOLOAD=1（双微动）、FILAMENT_RGB=1

用法:
    python tools/build_personal.py --dry-run     # 只列清单
    python tools/build_personal.py               # 编译
    python tools/build_personal.py --list "A1:0.20:P2S:0.90"   # 自定义

产物: firmwares_personal/<机型>_<槽位>_<模式>_<回抽>.bin
"""
import argparse
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "firmwares_personal")
PIO_ENV = "fw"
PIO = os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts", "platformio.exe")

# (显示名, 目录前缀, BAMBU_BUS_AMS_NUM)
SLOTS = [("AUTO", "ams_auto", "4"),
         ("A", "ams_a", "0"),
         ("B", "ams_b", "1"),
         ("C", "ams_c", "2"),
         ("D", "ams_d", "3")]

# (模式名, DBMCU_P1S, BMCU_SOFT_LOAD)
MODES = {
    "standard": ("0", "0"),
    "soft":     ("0", "1"),
    "highforce": ("1", "0"),
}

# 机器 → (模式, 回抽米)
PRESETS = {
    "A1":  ("standard", "0.20"),
    "P2S": ("highforce", "0.90"),
}

DM = "1"      # 双微动 / AUTOLOAD
RGB = "1"     # 在线灯显示耗材颜色


def build_one(machine, mode, retract, slot_name, slot_prefix, ams_num):
    env = dict(os.environ)
    env.update({
        "BAMBU_BUS_AMS_NUM": ams_num,
        "AMS_RETRACT_LEN": retract + "f",
        "BMCU_DM_TWO_MICROSWITCH": DM,
        "BMCU_ONLINE_LED_FILAMENT_RGB": RGB,
        "DBMCU_P1S": MODES[mode][0],
        "BMCU_SOFT_LOAD": MODES[mode][1],
    })
    name = "%s_%s_%s_%s.bin" % (machine, slot_name, mode, retract)
    out = os.path.join(OUT_DIR, name)

    print(">>> %s (AMS_NUM=%s, retract=%sf, P1S=%s, soft=%s)"
          % (name, ams_num, retract, MODES[mode][0], MODES[mode][1]), flush=True)
    r = subprocess.run([PIO, "run", "-e", PIO_ENV], cwd=ROOT, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        tail = r.stdout.decode("utf-8", "replace")[-800:]
        print("    失败:\n" + tail, flush=True)
        return None

    src = os.path.join(ROOT, ".pio", "build", PIO_ENV, "firmware.bin")
    if not os.path.exists(src):
        print("    没有 firmware.bin", flush=True)
        return None
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(src, "rb") as f:
        data = f.read()
    with open(out, "wb") as f:
        f.write(data)
    print("    OK  %d 字节  %s" % (len(data), hashlib.sha256(data).hexdigest()[:16]), flush=True)
    return out


def gen_version(bump=True):
    """调 tools/gen_version.py 生成版本号（一次构建一个版本，所有变体共用）"""
    cmd = [sys.executable, os.path.join(ROOT, "tools", "gen_version.py")]
    if not bump:
        cmd.append("--no-bump")
    r = subprocess.run(cmd, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", cwd=ROOT)
    if r.returncode != 0:
        raise SystemExit("版本号生成失败: " + (r.stderr or r.stdout))
    out = (r.stdout or "").strip()
    return out.splitlines()[-1].strip() if out else "?"


def make_manifest(version):
    """给 personal 目录生成 manifest（SHA256 + CRC32 + 大小）"""
    import zlib
    entries = []
    for fn in sorted(os.listdir(OUT_DIR)):
        if not fn.endswith(".bin"):
            continue
        p = os.path.join(OUT_DIR, fn)
        h = hashlib.sha256()
        crc = 0
        size = 0
        with open(p, "rb") as f:
            while True:
                b = f.read(1 << 20)
                if not b:
                    break
                size += len(b)
                crc = zlib.crc32(b, crc)
                h.update(b)
        entries.append((fn, h.hexdigest(), "%08X" % (crc & 0xFFFFFFFF), size))
    path = os.path.join(OUT_DIR, "manifest.txt")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# 个人固件 版本 %s\n" % version)
        f.write("# format: SHA256_HEX CRC32_HEX SIZE_BYTES REL_PATH\n")
        for fn, sha, c, size in entries:
            f.write("%s %s %d %s\n" % (sha, c, size, fn))
    return path, len(entries)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-bump", action="store_true", help="沿用当前版本号，不递增")
    ap.add_argument("--list", default=None, help='例如 "A1:0.20,P2S:0.90"')
    a = ap.parse_args()

    presets = dict(PRESETS)
    if a.list:
        presets = {}
        for item in a.list.split(","):
            m, r = item.split(":")
            presets[m.strip()] = (PRESETS.get(m.strip(), ("standard", r))[0], r.strip())

    jobs = [(m, mode, ret, s[0], s[1], s[2])
            for m, (mode, ret) in presets.items()
            for s in SLOTS]

    print("模式: AUTOLOAD=%s  RGB=%s" % (DM, RGB))
    print("共 %d 个固件:" % len(jobs))
    for j in jobs:
        print("  %-4s %-5s %-10s %s" % (j[0], j[3], j[2], j[1]))
    if a.dry_run:
        return 0

    version = gen_version(bump=not a.no_bump)
    print("版本号: %s" % version, flush=True)

    ok = 0
    for j in jobs:
        if build_one(*j):
            ok += 1
    path, n = make_manifest(version)
    print("\n完成 %d/%d，产物在 %s" % (ok, len(jobs), OUT_DIR))
    print("manifest: %s（%d 个）" % (path, n))
    return 0 if ok == len(jobs) else 1


if __name__ == "__main__":
    sys.exit(main())
