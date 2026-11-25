#pragma once
#include <stdint.h>
#include "ws2812.h"

extern WS2812_class RGBOUT[4];

static inline __attribute__((always_inline))
void mc_stu_rgb_set(uint8_t ch, uint8_t r, uint8_t g, uint8_t b)
{
    if (ch < 4) RGBOUT[ch].set_RGB(r, g, b, 0);
}

static inline __attribute__((always_inline))
void mc_pull_online_rgb_set(uint8_t ch, uint8_t r, uint8_t g, uint8_t b, bool filament = false)
{
    if (ch < 4) RGBOUT[ch].set_RGB_online(r, g, b, 1, filament);
}

void ams_datas_set_need_to_save_filament(uint8_t filament_idx);
void ams_state_claim_loaded(uint8_t filament_ch);
void ams_state_set_loaded(uint8_t filament_ch);
void ams_state_set_unloaded(uint8_t filament_ch);
uint8_t ams_state_get_loaded(void);

float motion_control_filament_meters(uint8_t ch);

// ==================== 诊断接口（0x04A1 / 0x04A2）====================
// 维护用只读诊断：一问一答，不参与注册握手，也不改运动状态机。
// 协议规格见哨兵仓库 docs/ROADMAP.md 第 4 节；下面注释里的 [n] 就是线上字节偏移。
#define BM_DIAG_VER        3u    // 协议版本（v3：36B，带固件版本+变体+序列号）
#define BM_DIAG_ITEM_SYS   0u    // item=0 系统健康（20 B）
#define BM_DIAG_ITEM_CH    1u    // item=1 四通道明细（42 B）
#define BM_DIAG_SYS_LEN    36u
#define BM_DIAG_CH_LEN     42u
#define BM_DIAG_LAST_SECS  0xFFFFu   // 「从未发生」/饱和值

typedef struct bm_diag_sys_s
{
    uint8_t  ver;             // [0]     协议版本
    uint8_t  slot;            // [1]     0~3 = AMS A/B/C/D
    uint8_t  sys_flags;       // [2]     见规格 4.4
    uint8_t  reset_cause;     // [3]     0=上电 1=引脚 2=软复位 3=IWDG 4=WWDG 5=其它
    uint32_t uptime_s;        // [4..7]  运行时长
    uint16_t reg_count;       // [8..9]  累计注册次数（本次上电）
    uint16_t last_reg_s;      // [10..11] 距上次成功注册的秒数
    uint8_t  fw_major;        // [12]    固件主版本（现在恒为 1）
    uint8_t  fw_year;         // [13]    年份后两位（26 = 2026）
    uint8_t  fw_week;         // [14]    周
    uint8_t  fw_build;        // [15]    构建号
    uint16_t loop_us_max;     // [16..17] 自上次查询以来的最大循环周期 µs（取完清零）
    uint8_t  loop_us_100us;   // [18]    循环周期滑动均值，100 µs 为单位
    uint8_t  variant;         // [19]    固件变体位：bit1:0 档位 0软/1标/2高，bit3:2 退料，bit4 双微动，bit5 灯，bit6 AUTO
    char     serial[15];      // [20..34] 15 位 ASCII 序列号（不含结尾 0）
    uint8_t  reserved;        // [35]    对齐保留，发 0
} __attribute__((packed)) bm_diag_sys_t;   // packed：字段偏移 == 线上偏移

typedef struct bm_diag_ch_s
{
    uint16_t pressure_x100;   // [0..1] 0~10000 = 0.00~100.00 %
    uint16_t buf_mv;          // [2..3] 压力传感器电压 mV
    uint16_t key_mv;          // [4..5] 触发输入电压 mV
    uint8_t  key_state;       // [6]    0=无 1=双 2=仅外部 3=其它
    uint16_t angle_x10;       // [7..8] 0~3600 = 0.0~360.0°
    uint8_t  ch_flags;        // [9]    见规格 4.4c
} __attribute__((packed)) bm_diag_ch_t;    // packed：字段偏移 == 线上偏移

// 启动时读并清复位原因标志（要求 time_hw_init 之后尽早调用）
void bm_diag_boot_init(void);
// 主循环每轮调用一次（循环周期统计）
void bm_diag_loop_tick(void);
// 状态跳变观察（只记录，不改流程）
void bm_diag_note_registered(void);
// 取快照：只读无副作用；sys 会顺带清掉 loop_us_max
void bm_diag_sys_get(bm_diag_sys_t *out);
bool bm_diag_channel_get(uint8_t ch, bm_diag_ch_t *out);
