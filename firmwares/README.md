# BMCU 改版固件（firmwares/）

> ⚠️ **当前这里的 60 个预编译包是「含诊断接口 v3」的版本**（01.26.47.09 及更早）。
> 含诊断接口（`0x04A1/0x04A2`）的源码已经实现（≥ `01.26.47.10`），但**要等硬件验证通过后再统一重建替换**。
> 想提前用诊断，只能在源码里自己编：`pio run -e <env>`（产物在 `.pio/build/<env>/firmware.bin`）。

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

> **单台或多台都建议按顺序刷写**
>
> - 只有一台：刷 `AMS_AUTO` 即可，上电自动协商槽位。
> - 同一总线多台：逐台刷、逐台确认注册成功再刷下一台；
>   固定槽位按 `AMS_A`→`B`→`C`→`D`，或全用 `AMS_AUTO` 但一台一台上电。
> - 全部接好一起刷容易抢同一槽位，也不好定位是哪台的问题。

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

## 版本号规则

固件版本形如 `01.26.47.07`，会烧进固件并显示在打印机/切片软件里：

```
01      固定主版本
26      年份（2026）
47      ISO 周数（第 47 周）
07      本周第几次构建（自动递增）
```

构建时由 `tools/gen_version.py` 自动生成，写入 `include/version_sg.h` 与仓库根的 `version` 文件。

## 序列号规则（打印机里能看到的身份码）

BMCU 上报给打印机的 AMS 序列号固定 **15 位**：

```
B 0 C  2 A C 7 D A 5  A 1 1 3 0
│      │              │ │ │ │ └── 耗材灯：1 = 开，0 = 关
│      │              │ │ │ └──── 退料长度：0=0.20f 1=0.50f 2=0.70f 3=0.90f
│      │              │ │ └────── 档位：1=soft 2=standard 3=high_force
│      │              │ └──────── 自动进料：1 = 开，0 = 关
│      │              └────────── 槽位：A~D = A~D 槽，0 = 自动注册
│      └───────────────────────── UID：芯片派生 7 位（28 位，2.68 亿种）
└──────────────────────────────── 标识：B0C = BMCU 开源固件
```

| 位 | 含义 | 取值 |
|---|---|---|
| 1~3 | BMCU 标识 | 固定 `B0C` |
| 4~10 | 芯片 UID 派生 | 7 位十六进制（28 位，2.68 亿种）|
| 11 | 槽位 | `A`~`D`=A~D 槽，`0`=自动注册(AUTO) |
| 12 | 自动进料 | `1`=双微动自动上料，`0`=关闭 |
| 13 | 进料档位 | `1`=轻柔(soft_load)，`2`=标准(standard)，`3`=高推力(high_force_load)（数字越大力度越大）|
| 14 | 退料长度 | `0`=0.20f，`1`=0.50f，`2`=0.70f，`3`=0.90f |
| 15 | 耗材灯 | `1`=在线灯显示耗材颜色，`0`=关闭 |

> **看配置只需要最后 5 位**：`A1130` = A 槽 / 自动进料开 / soft_load / 0.90f / 耗材灯关。
> 官方 AMS 序列号同样是 15 位，长度与显示格式兼容。
> 序列号里的槽位字符只用于分辨刷写的是哪一个变体固件；实际槽位由总线地址或 `AMS_AUTO` 协商决定，不参与寻址。

### 举例

```
A 槽 · 自动进料开 · soft_load(1) · 0.90f · 耗材灯关
  ↓
B0C 2AC7DA5 A1130     ← 标识 + UID 7 位 + 配置 5 位
```

### 为什么打印机界面里有时显示旧序列号

打印机在**第一次识别**某台 AMS 时把序列号记进登记表，之后即使固件更新、序列号变了，界面也不会自动刷新。

要看新序列号：打印机断电 → 断开 AMS 电源 → 打印机开机 → 再给 AMS 上电。

## 60 个变体对照表

`????????` 为各芯片独有的 7 位 UID。

| 档位 | 槽位 | 退料长度 | 序列号（默认 自动进料=1、耗材灯=0）|
|---|---|---|---|
| soft_load | AMS_AUTO | 0.20f | `B0C????????01100` |
| soft_load | AMS_AUTO | 0.50f | `B0C????????01110` |
| soft_load | AMS_AUTO | 0.70f | `B0C????????01120` |
| soft_load | AMS_AUTO | 0.90f | `B0C????????01130` |
| soft_load | AMS_A | 0.20f | `B0C????????A1100` |
| soft_load | AMS_A | 0.50f | `B0C????????A1110` |
| soft_load | AMS_A | 0.70f | `B0C????????A1120` |
| soft_load | AMS_A | 0.90f | `B0C????????A1130` |
| soft_load | AMS_B | 0.20f | `B0C????????B1100` |
| soft_load | AMS_B | 0.50f | `B0C????????B1110` |
| soft_load | AMS_B | 0.70f | `B0C????????B1120` |
| soft_load | AMS_B | 0.90f | `B0C????????B1130` |
| soft_load | AMS_C | 0.20f | `B0C????????C1100` |
| soft_load | AMS_C | 0.50f | `B0C????????C1110` |
| soft_load | AMS_C | 0.70f | `B0C????????C1120` |
| soft_load | AMS_C | 0.90f | `B0C????????C1130` |
| soft_load | AMS_D | 0.20f | `B0C????????D1100` |
| soft_load | AMS_D | 0.50f | `B0C????????D1110` |
| soft_load | AMS_D | 0.70f | `B0C????????D1120` |
| soft_load | AMS_D | 0.90f | `B0C????????D1130` |
| standard | AMS_AUTO | 0.20f | `B0C????????01200` |
| standard | AMS_AUTO | 0.50f | `B0C????????01210` |
| standard | AMS_AUTO | 0.70f | `B0C????????01220` |
| standard | AMS_AUTO | 0.90f | `B0C????????01230` |
| standard | AMS_A | 0.20f | `B0C????????A1200` |
| standard | AMS_A | 0.50f | `B0C????????A1210` |
| standard | AMS_A | 0.70f | `B0C????????A1220` |
| standard | AMS_A | 0.90f | `B0C????????A1230` |
| standard | AMS_B | 0.20f | `B0C????????B1200` |
| standard | AMS_B | 0.50f | `B0C????????B1210` |
| standard | AMS_B | 0.70f | `B0C????????B1220` |
| standard | AMS_B | 0.90f | `B0C????????B1230` |
| standard | AMS_C | 0.20f | `B0C????????C1200` |
| standard | AMS_C | 0.50f | `B0C????????C1210` |
| standard | AMS_C | 0.70f | `B0C????????C1220` |
| standard | AMS_C | 0.90f | `B0C????????C1230` |
| standard | AMS_D | 0.20f | `B0C????????D1200` |
| standard | AMS_D | 0.50f | `B0C????????D1210` |
| standard | AMS_D | 0.70f | `B0C????????D1220` |
| standard | AMS_D | 0.90f | `B0C????????D1230` |
| high_force_load | AMS_AUTO | 0.20f | `B0C????????01300` |
| high_force_load | AMS_AUTO | 0.50f | `B0C????????01310` |
| high_force_load | AMS_AUTO | 0.70f | `B0C????????01320` |
| high_force_load | AMS_AUTO | 0.90f | `B0C????????01330` |
| high_force_load | AMS_A | 0.20f | `B0C????????A1300` |
| high_force_load | AMS_A | 0.50f | `B0C????????A1310` |
| high_force_load | AMS_A | 0.70f | `B0C????????A1320` |
| high_force_load | AMS_A | 0.90f | `B0C????????A1330` |
| high_force_load | AMS_B | 0.20f | `B0C????????B1300` |
| high_force_load | AMS_B | 0.50f | `B0C????????B1310` |
| high_force_load | AMS_B | 0.70f | `B0C????????B1320` |
| high_force_load | AMS_B | 0.90f | `B0C????????B1330` |
| high_force_load | AMS_C | 0.20f | `B0C????????C1300` |
| high_force_load | AMS_C | 0.50f | `B0C????????C1310` |
| high_force_load | AMS_C | 0.70f | `B0C????????C1320` |
| high_force_load | AMS_C | 0.90f | `B0C????????C1330` |
| high_force_load | AMS_D | 0.20f | `B0C????????D1300` |
| high_force_load | AMS_D | 0.50f | `B0C????????D1310` |
| high_force_load | AMS_D | 0.70f | `B0C????????D1320` |
| high_force_load | AMS_D | 0.90f | `B0C????????D1330` |

## 测试用固件（仅内部）

`platformio.ini` 里有两个测试环境，把 UID 段固定成 `ABCDEFG`，方便确认显示链路：

```
pio run -e ams_a_090_serialtest    → 显示 B0CABCDEFGA1130
pio run -e ams_b_090_serialtest    → 显示 B0CABCDEFGB1210
```

不用于发布。

## 配套

需要 **BMCU 总线哨兵（BMCU-BusSentry-SG）** 才能使用自动续期（静默指令 0x04A0）。

*作者：时光凉了年少梦 · 2026*
