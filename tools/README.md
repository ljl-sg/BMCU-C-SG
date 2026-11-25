# 工具目录

本目录存放构建、版本号与烧录相关工具。

| 文件 | 作用 | 用法 |
|---|---|---|
| `gen_version.py` | 生成构建追踪版本号（`01.<年>.<ISO周>.<序号>`）| `python tools/gen_version.py`（自动 +1）|
| `build_all_firmwares.py`（仓库根）| 一键编译 60 个变体 + 生成 manifest | `python build_all_firmwares.py -j 8` |
| `build_personal.py` | 只编自己用的几个组合（不走全量）| `python tools/build_personal.py --dry-run` |
| `bambu_parse.py` | 离线解析 BambuBus 抓包文件（帧统计、拓扑、命令码、筛选）| `python tools/bambu_parse.py cap.bin --stats --topo` |
| `capture.py` | 从总线哨兵板拉取原始字节流并落盘（TCP 81）| `python tools/capture.py <哨兵IP> 81 my_cap` |

---

## 烧录工具（WCHISPTool）

给 **CH32V203**（BMCU 主控）烧录固件用。本仓库**不再附带**安装包，请到 WCH 官网下载最新版：

| 项 | 信息 |
|---|---|
| 下载 | <https://www.wch.cn/downloads/WCHISPTool_Setup_exe.html> |
| 芯片 | **CH32V203C8T6** |
| 写入地址 | **0x00000000** |

### 使用

1. 从官网下载并安装（含 USB 驱动）
2. 插入 USB 转串口模块，运行 WCHISPTool
3. 芯片选 **CH32V203C8T6**，写入地址 **0x00000000**，选择 `firmwares/` 里的 bin 下载

> 完整烧录步骤见主文档 [`../README.md`](../README.md) 的「烧录」章节。

## capture.py（抓包采集）

从 **BMCU-BusSentry-SG 总线哨兵** 的 **TCP 81** 端口拉原始字节流，落盘为 `.bin` + `.csv`（时间戳）。

```bash
python tools/capture.py 192.168.1.50        # 默认 81 端口，自动命名
python tools/capture.py 192.168.1.50 81 我的抓包
```

> 需要搭配 **总线哨兵板**（固件内置 TCP 81 推流，默认关闭，在网页「数据分析」里打开）。
> 抓完用 `bambu_parse.py` 解析。
>
> 总线哨兵立创开源工程：https://oshwhub.com/sg-diy/project_tactlhci

## bambu_parse.py（抓包解析）


解析总线原始字节流（`.bin`），用于排查通信问题。数据来源任意 485 抓取工具即可，
例如配套的 **BMCU-BusSentry-SG 总线哨兵**（TCP 81 推流 + `capture.py`）。

| 参数 | 作用 |
|---|---|
| `--stats` | 帧统计（数量、覆盖率、速率）|
| `--topo` | 通信拓扑（src → tgt）|
| `--cmds` | 命令码清单 |
| `--find 0x040D` | 筛选指定命令码的帧 |
| `--dump N` | 打印前 N 帧原始内容 |

> 该工具在本仓库维护。配套的 BMCU-BusSentry-SG 总线哨兵**只提供固件与硬件资料，不公开源码**。

