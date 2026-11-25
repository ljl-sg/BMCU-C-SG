#!/usr/bin/env python3
"""BMCU 固件一键编译（60 变体）

矩阵：
    档位 3  × 槽位 5  × 退料长度 4 = 60

    档位        high_force_load / standard / soft_load
    槽位        A / B / C / D / AUTO（自动注册槽位，BAMBU_BUS_AMS_NUM=4）
    退料长度    0.20f / 0.50f / 0.70f / 0.90f

输出结构（与上游一致）：
    firmwares/
    ├── high_force_load/AMS_A/ams_a_0.20f.bin … 0.90f.bin
    ├── high_force_load/AMS_AUTO/ams_auto_0.20f.bin … 0.90f.bin
    ├── standard/AMS_A/ … AMS_AUTO/
    ├── soft_load/AMS_A/ … AMS_AUTO/
    ├── manifest.txt
    ├── README.md
    └── which_to_choose.txt

默认编译开关（已在 platformio.ini 里固定）：
    -DBMCU_DM_TWO_MICROSWITCH=1           自动进料
    （指示灯耗材颜色默认关闭，需要时在对应 env 里加 -DBMCU_ONLINE_LED_FILAMENT_RGB=1）

版本号：
    默认编译前自动调用 tools/gen_version.py 递增版本号（写入 version 与 include/version_sg.h），
    想沿用当前版本号用 --no-bump。

用法：
    python build_all_firmwares.py                # 全量 60 个（自动递增版本号）
    python build_all_firmwares.py --no-bump      # 沿用当前版本号
    python build_all_firmwares.py -j 4           # 4 并发（默认 4）
    python build_all_firmwares.py --dry-run      # 只列出不编译
    python build_all_firmwares.py --clean        # 先清空 firmwares/
    python build_all_firmwares.py --limit 8      # 只编前 8 个（测试用）
"""
import argparse
import concurrent.futures as cf
import hashlib
import os
import shutil
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(ROOT, "firmwares")
PIO = os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Scripts", "platformio.exe")

# (显示名, 环境后缀, pio 宏说明)
TIERS = [
    ("high_force_load", "_p1s"),
    ("standard",       ""),
    ("soft_load",      "_soft"),
]

SLOTS = [
    ("a",    "AMS_A"),      # BAMBU_BUS_AMS_NUM=0
    ("b",    "AMS_B"),      # 1
    ("c",    "AMS_C"),      # 2
    ("d",    "AMS_D"),      # 3
    ("auto", "AMS_AUTO"),   # 4 = 自动注册槽位
]

# 环境名里的长度编码 -> 文件名里的长度
LENGTHS = [
    ("020", "0.20f"),
    ("050", "0.50f"),
    ("070", "0.70f"),
    ("090", "0.90f"),
]

README = """# BMCU 改版固件（firmwares/）

固件按上游惯例分档存放。**每个档位下按槽位分目录，目录内是该槽位的全部退料长度版本。**

## 目录结构

```
firmwares/
├── high_force_load/   ← 高推力档（P 系列推荐）
│   ├── AMS_A/ams_a_0.20f.bin  ams_a_0.50f.bin  ams_a_0.70f.bin  ams_a_0.90f.bin
│   ├── AMS_B/  AMS_C/  AMS_D/
│   ├── AMS_AUTO/ams_auto_0.20f.bin … 0.90f.bin   ← 自动注册槽位
├── standard/          ← 标准档（A1 推荐）
├── soft_load/         ← 轻柔档（细线材 / 易卡料友好）
└── manifest.txt
```

> 档位只是**推荐**，固件本身通用，任何机型都可以自己选。

## 三个档位怎么选

| 档位 | 进料快速段 | 硬停阈值 | 适用 |
|---|---|---|---|
| high_force_load | 88% | 97% | P 系列、长走线、阻力大 |
| standard | 标准 | 标准 | 大多数情况 |
| soft_load | 75% | 90% | 细线材、易卡料、送料顺滑 |

## 槽位怎么选

**优先用 `AMS_AUTO`（自动注册槽位）**：固件把 `BAMBU_BUS_AMS_NUM` 设成 4，上电后跟打印机协商自己占哪个 AMS 口，不用管插在 A 还是 B。一条总线上挂多个 BMCU 时尤其省事。

只有在自动注册不好使（打印机固件较老、或只认固定变体）时，才退回固定槽位：BMCU 接在打印机的第几个 AMS 口，就选对应字母（AMS_A ~ AMS_D）。

## 退料长度怎么选

文件名里的 `0.20f` ~ `0.90f` 是**退料长度**（越大退得越多）：

- 从 `0.50f` 开始试
- 退料后耗材仍卡在挤出机 → 调大（0.70f / 0.90f）
- 退料过多、耗材脱离缓冲区 → 调小（0.20f）

## 默认已启用的功能

- **自动进料**（双微动检测，`BMCU_DM_TWO_MICROSWITCH=1`）

状态指示灯的耗材颜色显示**默认关闭**；需要时在对应 env 里加 `-DBMCU_ONLINE_LED_FILAMENT_RGB=1` 再编。

## 烧录

1. BMCU 板按住 BOOT 上电进入下载模式
2. WCHISP 选择芯片 **CH32V203C8T6**
3. 选择对应 `firmware.bin`，写入地址 **0x00000000**
4. 烧录后断电重上电

> BMCU 固件约 52 KB。若提示「文件过大」，说明选错了文件（那多半是哨兵的 ESP8266 固件，约 530 KB）。

## 配套

需要 **BMCU 总线哨兵（BMCU-BusSentry-SG）** 才能使用自动续期（静默指令 0x04A0）。

*作者：时光凉了年少梦 · 2026*
"""

WHICH = """BMCU 固件选择指引
=================

1. 先选档位
   P1S / 长走线 / 阻力大   -> high_force_load
   大多数情况              -> standard
   细线材 / 容易卡料       -> soft_load

2. 再选槽位
   优先 AMS_AUTO（自动注册槽位，不用管插在哪个口）
   固定变体才用 AMS_A / AMS_B / AMS_C / AMS_D

3. 最后选退料长度
   默认 0.50f
   退料不干净 -> 0.70f 或 0.90f
   退料过多   -> 0.20f

档位只是推荐，任何机型都可以用任一档位。
"""


def jobs():
    """生成 (tier_dir, slot, env_name, out_relpath) 列表"""
    out = []
    for tier_dir, suffix in TIERS:
        for slot, dirname in SLOTS:
            for code, label in LENGTHS:
                env = f"ams_{slot}_{code}{suffix}"
                rel = os.path.join(tier_dir, dirname, f"ams_{slot}_{label}.bin")
                out.append((tier_dir, slot, env, rel))
    return out


def build_one(env):
    r = subprocess.run([PIO, "run", "-e", env], cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return env, r.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", "--jobs", type=int, default=4, help="并发数（默认 4）")
    ap.add_argument("--dry-run", action="store_true", help="只列出")
    ap.add_argument("--clean", action="store_true", help="先清空 firmwares/")
    ap.add_argument("--limit", type=int, default=0, help="只编前 N 个（测试）")
    ap.add_argument("--no-bump", action="store_true", help="不递增版本号，沿用当前值")
    args = ap.parse_args()

    # ---- 编译前先确定版本号 ----
    gen = os.path.join(ROOT, "tools", "gen_version.py")
    if not args.no_bump and not args.dry_run and os.path.exists(gen):
        r = subprocess.run([sys.executable, gen], cwd=ROOT,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = r.stdout.decode("utf-8", "replace").strip().splitlines()
        if out:
            print("  版本号:", out[-1])
        if r.returncode != 0:
            print("  版本号生成失败，沿用当前 version")
    else:
        try:
            cur = open(os.path.join(ROOT, "version"), encoding="utf-8").read().strip()
            why = "dry-run，未改版本号" if args.dry_run else "--no-bump，未改版本号"
            print("  版本号:", cur, "（%s）" % why)
        except Exception:
            pass

    items = jobs()
    if args.limit:
        items = items[:args.limit]

    print(f"  变体总数: {len(items)}  并发: {args.jobs}")
    if args.dry_run:
        for tier_dir, slot, env, rel in items:
            print(f"    {env:24s} -> firmwares/{rel}")
        return

    if args.clean and os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR)
    os.makedirs(OUT_DIR, exist_ok=True)

    envs = sorted({it[2] for it in items})
    failed = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(build_one, e): e for e in envs}
        done = 0
        for fut in cf.as_completed(futs):
            env, code = fut.result()
            done += 1
            if code != 0:
                failed.append(env)
                print(f"  [{done}/{len(envs)}] FAIL {env}")
            else:
                print(f"  [{done}/{len(envs)}] OK   {env}")

    if failed:
        print(f"\n  编译失败 {len(failed)} 个: {', '.join(failed)}")
        print("  继续收集已成功的产物…")

    manifest = []
    ok = 0
    for tier_dir, slot, env, rel in items:
        src = os.path.join(ROOT, ".pio", "build", env, "firmware.bin")
        if not os.path.exists(src):
            continue
        dst = os.path.join(OUT_DIR, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        data = open(dst, "rb").read()
        manifest.append("\t".join([
            rel.replace("\\", "/"),
            str(len(data)),
            hashlib.sha256(data).hexdigest(),
            f"{zlib.crc32(data) & 0xFFFFFFFF:08x}",
        ]))
        ok += 1

    with open(os.path.join(OUT_DIR, "manifest.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# 路径\t大小\tSHA256\tCRC32\n")
        f.write("\n".join(manifest) + "\n")

    # README.md 是手写文档（含版本号规则 / 序列号规则 / 60 变体对照表），
    # 构建时不能覆盖，否则每次编译都会冲掉这些说明。只更新 manifest 与 which_to_choose。

    with open(os.path.join(OUT_DIR, "which_to_choose.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(WHICH)

    print(f"\n  完成: {ok}/{len(items)} 个固件 -> {OUT_DIR}")
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
