# BMCU 固件代码地图

> 目的：让人在 10 分钟内知道「哪个文件管什么、改一处要动哪里」。
> 最后更新：2026-11-22（P0-1 诊断接口 + P2-1 服务表 + P2-5 命名全量小写化）

---

## 一、目录结构

```
src/
├── app/          应用层：启动、主循环、数据模型、诊断
│   ├── main.cpp
│   ├── ams.cpp / ams.h
│   ├── app_api.h             对外暴露的小接口（灯、状态、米数、诊断）
│   └── diag.cpp              诊断接口 0x04A1/0x04A2（复位原因/计数/循环周期）
│
├── bus/          总线层：BambuBus / aHub / 物理层 / 序列号 / 静默
│   ├── bambu_bus_ams.cpp/.h  协议核心（解析、注册、命令分发）
│   ├── ahub_bus.cpp/.h       旧 aHub 协议
│   ├── _bus_hardware.cpp/.h  RS485 物理层（UART+DMA+DE）
│   ├── crc_bus.c/.h          CRC8 / CRC16
│   ├── seq_no.cpp/.h         序列号构造（15 位 + 显示字段）
│   └── vendor_mute.cpp/.h    静默指令状态机（0x04A0）
│
├── motion/       运动控制：进料/退料、压力、编码器、标定
│   ├── Motion_control.cpp/.h
│   ├── MC_PULL_calibration.cpp/.h
│   ├── ADC_DMA.cpp/.h
│   └── many_soft_AS5600.cpp/.h
│
├── storage/      持久化
│   └── Flash_saves.cpp/.h
│
├── drivers/      板级外设驱动
│   └── ws2812.cpp/.h         灯带驱动（状态指示灯硬件层）
│
├── log/          日志
│   └── Debug_log.cpp/.h      调试串口日志（UART3 115200，默认关闭）
│
├── config/       编译期配置
│   └── ams_addressing.h      槽位宏（AMS_NUM / AMS_AUTO）
│
└── hal/          硬件抽象层（HAL）
    ├── time_hw.c/.h          计时（ticks/us/ms）
    └── irq_wch.h             临界区保护
```

构建说明：`platformio.ini` 的 `[env:base]` 里已加 `-Isrc/app` 等 7 条路径，
所有 env（117 个，含 base/fw/moj）继承它，所以 `#include "xxx.h"` 不需要写相对路径。

---

## 一·五、命名约定（2026-11-22 全量统一）

```
模块前缀（函数 / 全局变量 / 常量一律「小写下划线 + 模块前缀」）

bambubus_   BambuBus 协议（bambubus_run / bambubus_package_type）
ahubus_     旧 aHub 协议（ahubus_run / ahubus_package_type）
mc_         运动控制（mc_pull_pct / mc_pull_v_offset / mc_as5600 / mc_diag_channel）
ams_        数据模型与状态（ams_state_get_loaded / ams_data_save_run）
flash_      持久化（flash_saves_init / flash_ams_state_read / flash_motion_read）
bm_diag_    诊断接口（app/diag.cpp：bm_diag_sys_get / bm_diag_channel_get）
diag_       诊断协议层内部 static（get_package_diag_request / diag_build_sys）
```

- **函数 / 全局变量 / 常量**：全小写下划线（例 `mc_on_use_target_pct`、`mc_pull_pct_f`）
- **文件内常量**：保留 `kCamelCase`（例 `kChCount`、`kAS5600_PI`）
- **类型**：小写下划线（例 `bambubus_long_packet_data`、`flash_filamentinfo`）；
  上游的 `_ams` / `_filament` 保持原样
- **文件与目录名不动**（`MC_PULL_calibration.cpp`、`Motion_control.cpp` 等）——
  改名会牵动 `#include`、`-Isrc/...` 和所有文档
- 新增代码一律按此写；上游 V6~V8 带入的旧名已于 2026-11-22 全量改齐

> 改名验证：三个 env 编译后 Flash 用量与改名前一模一样（55700 / 55800 / 57752 B），
> 且 `firmware.bin` SHA256 **逐字节相同**（A `B1CDD0D4…`、B `D76496CB…`、调试版 `20CD85C7…`）。

---

## 二、分层与数据流

```
                  ┌──────────────────────────┐
                  │  app/main.cpp            │
                  │  setup() → while(1)      │
                  └──────┬───────────────────┘
                         │
    ┌────────────────────┼────────────────────┐
    │                    │                    │
┌───▼─────────┐   ┌──────▼────────┐   ┌───────▼────────┐
│  bus/       │   │  motion/      │   │  storage/ ui/  │
│ 协议与物理层 │   │ 运动控制      │   │ 存储与显示     │
└───┬─────────┘   └──────┬────────┘   └───────┬────────┘
    │                    │                    │
    └────────────────────┼────────────────────┘
                         │
                ┌────────▼─────────┐
                │  app/ams.h       │
                │  _ams[4]         │
                │   └ _filament[4] │
                └──────────────────┘
```

主循环每轮（`app/main.cpp`）：

```
bus_rx_poll()                    从 DMA 缓冲取字节并做帧同步
  ↓
ahubus_run() / bambubus_run()    两套协议各自跑状态机
  ↓
bus_port_to_host.send_package()  发应答
  ↓
状态脏了 → 写 Flash（仅总线空闲时）
  ↓
motion_control_run(error)        电机控制（每轮必跑）
Flash_background_run() / RGB_update()
```

---

## 三、文件职责

### bus/（总线层）

| 文件 | 行数 | 职责 | 改动风险 |
|---|---|---|---|
| `bambu_bus_ams.cpp` | ~1711 | 帧解析、命令分发、在线检测/注册、序列号应答、诊断请求 0x04A1 | **高** |
| `ahub_bus.cpp` | ~467 | 旧 aHub 协议 | 中 |
| `_bus_hardware.cpp` | ~368 | UART1+DMA 收发、半双工方向、中断 | **高** |
| `seq_no.cpp` | ~151 | 序列号 15 位 + 显示字段打包 | 低 |
| `vendor_mute.cpp` | ~38 | 静默窗口状态机 | 低 |
| `crc_bus.c` | ~83 | CRC 查表 | 低 |

### motion/（运动控制）

| 文件 | 行数 | 职责 | 改动风险 |
|---|---|---|---|
| `Motion_control.cpp` | **~3311** | 4 通道状态机、PWM、压力闭环、微动、编码器、RGB；诊断只读访问器 `mc_diag_channel` | **高** |
| `MC_PULL_calibration.cpp` | ~433 | 送料/退料力度标定 | 中 |
| `ADC_DMA.cpp` | ~354 | 4 路压力采样 | 中 |
| `many_soft_AS5600.cpp` | ~352 | 磁编码器（软 I2C），角度→米数 | 中 |

### app/ storage/ drivers/ log/ config/ hal/

| 文件 | 行数 | 职责 |
|---|---|---|
| `app/main.cpp` | ~419 | 初始化 + 主循环调度（启动读复位原因、每轮循环周期打点）；`-DDebug_log_on` 时启用服务表/慢 tick/启动分段打点 |
| `app/ams.h` / `ams.cpp` | 104 / 20 | 数据结构 `_ams` / `_filament` |
| `app/app_api.h` | 76 | 对外小接口（灯、状态、米数）+ 诊断类型与 API 声明 |
| `app/diag.cpp` | 169 | 诊断接口（0x04A1/0x04A2）：复位原因、运行时长、上次注册、固件版本、循环周期、四通道快照 |
| `storage/Flash_saves.cpp` | ~727 | 双页 NVM + 磨损均衡 |
| `drivers/ws2812.cpp` | ~195 | 灯带驱动（状态指示灯）|
| `log/Debug_log.cpp` | ~150 | 串口日志（UART3 115200 DMA；`Debug_log_on` 默认注释掉 → 正式固件零开销，调试时加 `-DDebug_log_on` 开启）|
| `config/ams_addressing.h` | 20 | 槽位宏 |
| `hal/time_hw.c/h` | — | 计时 |
| `hal/irq_wch.h` | — | 临界区 |

---

## 四、关键全局变量

| 变量 | 位置 | 含义 |
|---|---|---|
| `ams[4]` | `app/ams.cpp` | 4 个 AMS 槽数据 |
| `bus_host_device_type` | `bus/_bus_hardware.cpp` | 当前总线对端类型 |
| `printer_data_long` | `bus/bambu_bus_ams.cpp` | 最近一次解析的长帧 |
| `have_registered` | `bus/bambu_bus_ams.cpp` | 注册握手是否完成 |
| `auto_ams_num` | `bus/bambu_bus_ams.cpp` | AMS_AUTO 协商到的槽位 |
| `g_state_dirty` / `g_fil_dirty` | `app/main.cpp` | Flash 脏标志 |
| `rx_errors` | `bus/_bus_hardware.h` | 总线错误计数（帧头/长度/CRC8 错），诊断上报 |
| `s_reset_cause` / `s_loop_us` 等 | `app/diag.cpp` | 诊断统计（静态量，只经 `bm_diag_*()` 暴露）|

---

## 五、常见改动指南

| 我想改… | 去哪里 |
|---|---|
| 序列号格式 | `bus/seq_no.cpp`（`seq_no_build`）|
| 静默时长/行为 | `bus/vendor_mute.cpp` + `bambu_bus_ams.cpp` 的 `get_package_vendor_mute` |
| 新增总线命令 | `bus/bambu_bus_ams.cpp`：`get_packet_type` 加 case + 分发处 |
| 诊断字段/上报内容 | `app/diag.cpp`（取值与统计）+ `bus/bambu_bus_ams.cpp` 的 `get_package_diag_request`（组包）|
| 诊断协议字节布局 | 哨兵仓库 `docs/ROADMAP.md` 第 4 节（**唯一契约**）|
| 槽位/自动寻址 | `config/ams_addressing.h` + `bambu_bus_ams.cpp` 的 online detect |
| 进料力度/阈值 | `motion/MC_PULL_calibration.cpp`（参数）+ `Motion_control.cpp`（判定）|
| 持久化字段 | `storage/Flash_saves.cpp`（结构体 + pack/unpack）|
| 版本号 | `tools/gen_version.py` → `include/version_sg.h`，不要手改 |

---

## 六、已知痛点（后续重构目标）

| 问题 | 位置 | 影响 |
|---|---|---|
| 单文件过大 | `motion/Motion_control.cpp` 3311 行 | 定位困难 |
| 协议文件仍偏大 | `bus/bambu_bus_ams.cpp` 1668 行 | 解析与业务混杂 |
| 自动寻址未拆 | `bambu_bus_ams.cpp` 内 `#if BMCU_AMS_AUTO` | 阅读需预处理 |
| ~~命名风格混杂~~ | ~~`Motion_control_run` / `bambubus_*` / `MC_STU_RGB_*`~~ | ✅ 2026-11-22 已全量小写化（见「一·五、命名约定」）|
| 协议实现重复 | `ahub_bus.cpp` 与 `bambu_bus_ams.cpp` | 改一处要同步两处 |

---

*2026-11-22*
