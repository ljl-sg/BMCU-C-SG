#!/usr/bin/env python3
"""版本号生成器

规则: 01.<两位年>.<ISO周>.<当周第几次构建>（跨周重置）
例:   01.26.47.01 = 2026 年第 47 周第 1 次构建

用法:
    python tools/gen_version.py            # 递增构建号并写入
    python tools/gen_version.py --no-bump  # 不递增，沿用当前版本
    python tools/gen_version.py --set 01.26.47.03
    python tools/gen_version.py --show     # 只显示，不写

写入两个地方:
    version                 仓库根目录（上游约定的发布标记）
    include/version_sg.h    固件编译用的宏
"""
import argparse
import os
import re
from datetime import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VER_FILE = os.path.join(ROOT, "version")
HDR_FILE = os.path.join(ROOT, "include", "version_sg.h")
MAJOR = 1

PAT = re.compile(r"^(\d+)\.(\d+)\.(\d+)\.(\d+)$")


def read_current():
    if not os.path.exists(VER_FILE):
        return None
    try:
        t = open(VER_FILE, encoding="utf-8").read().strip()
    except Exception:
        return None
    m = PAT.match(t)
    return tuple(int(x) for x in m.groups()) if m else None


def compute(bump=True):
    now = datetime.now()
    cur = read_current()
    if not bump and cur:
        return cur
    # 构建号按 ISO 周累计：同年同周继续 +1，跨周/跨年重置为 1
    iso = now.isocalendar()
    if cur and cur[1] == (iso.year % 100) and cur[2] == iso.week:
        build = cur[3] + 1
    else:
        build = 1
    return (MAJOR, iso.year % 100, iso.week, build)


def fmt(v):
    return "%02d.%02d.%02d.%02d" % v


def write_all(v):
    with open(VER_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write(fmt(v) + "\n")

    os.makedirs(os.path.dirname(HDR_FILE), exist_ok=True)
    with open(HDR_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write("// 自动生成，请勿手动编辑\n")
        f.write("// 由 tools/gen_version.py 生成，格式 01.年.周.当周第几次构建（ISO 周）\n")
        f.write("#pragma once\n\n")
        f.write("#define SG_VER_MAJOR %d\n" % v[0])
        f.write("#define SG_VER_YEAR  %d\n" % v[1])
        f.write("#define SG_VER_WEEK  %d\n" % v[2])
        f.write("#define SG_VER_BUILD %d\n" % v[3])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-bump", action="store_true", help="不递增构建号")
    ap.add_argument("--set", default="", help="直接指定版本，如 01.10.26.03")
    ap.add_argument("--show", action="store_true", help="只显示不写入")
    args = ap.parse_args()

    if args.set:
        m = PAT.match(args.set.strip())
        if not m:
            raise SystemExit("版本格式不对，应为 01.年.周.构建号")
        v = tuple(int(x) for x in m.groups())
    else:
        v = compute(bump=not args.no_bump)

    if args.show:
        print(fmt(v))
        return

    write_all(v)
    print(fmt(v))


if __name__ == "__main__":
    main()
