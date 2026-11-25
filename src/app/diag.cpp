// ============================================================================
// 文件：diag.cpp
// 职责：BMCU 诊断接口（0x04A1/0x04A2）的数据采集
//   复位原因 · 运行时长 · 上次注册 · 固件版本 · 循环周期峰值/均值
// 主要接口：bm_diag_boot_init / bm_diag_loop_tick / bm_diag_sys_get
//           bm_diag_channel_get / bm_diag_note_registered
// 依赖：app_api（类型）、Motion_control（通道访问器）、bambu_bus_ams（注册状态）、
//       vendor_mute（静默状态）、time_hw（计时）、version_sg（固件版本）、seq_no（变体位）
// 注意：全部为只读观察，不得在此触发状态迁移或改写任何控制量
// ============================================================================
#include "app_api.h"

#include <string.h>

#include "bambu_bus_ams.h"
#include "seq_no.h"
#include "version_sg.h"
#include "vendor_mute.h"
#include "Motion_control.h"
#include "_bus_hardware.h"
#include "hal/time_hw.h"
#include "ch32v20x.h"

// 结构体字节数必须与线上布局一致（bambu_bus_ams.cpp 按固定偏移组包）
static_assert(sizeof(bm_diag_sys_t) == BM_DIAG_SYS_LEN, "bm_diag_sys_t 尺寸不符");
static_assert(sizeof(bm_diag_ch_t) == 10u, "bm_diag_ch_t 尺寸不符");

// 复位原因取值（与规格 4.3 的 reset_cause 一致）
enum : uint8_t
{
    BM_RST_POWER = 0,   // 上电
    BM_RST_PIN   = 1,   // 引脚复位
    BM_RST_SOFT  = 2,   // 软件复位（哨兵静默后 NVIC_SystemReset）
    BM_RST_IWDG  = 3,   // 独立看门狗
    BM_RST_WWDG  = 4,   // 窗口看门狗
    BM_RST_OTHER = 5,   // 其它
};

static uint8_t  s_reset_cause    = BM_RST_OTHER;
static uint32_t s_boot_ticks     = 0u;
static uint16_t s_reg_count      = 0u;
static uint32_t s_last_reg_tick  = 0u;
static bool     s_reg_ever       = false;
static uint16_t s_loop_us        = 0u;   // 滑动均值
static uint16_t s_loop_us_max    = 0u;   // 自上次查询以来的最大值
static uint32_t s_loop_prev      = 0u;
static bool     s_loop_prev_ok   = false;

static uint32_t diag_since_s(uint32_t since_ticks)
{
    const uint32_t tpm = time_hw_tpms;
    if (!tpm) return 0u;
    return (time_diff_u32(time_ticks32(), since_ticks) / tpm) / 1000u;
}

static uint16_t sat_u16(uint32_t v)
{
    return (uint16_t)(v > 0xFFFFu ? 0xFFFFu : v);
}

void bm_diag_boot_init(void)
{
    uint8_t cause = BM_RST_OTHER;

    if      (RCC_GetFlagStatus(RCC_FLAG_PORRST)  != RESET) cause = BM_RST_POWER;
    else if (RCC_GetFlagStatus(RCC_FLAG_PINRST)  != RESET) cause = BM_RST_PIN;
    else if (RCC_GetFlagStatus(RCC_FLAG_SFTRST)  != RESET) cause = BM_RST_SOFT;
    else if (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) cause = BM_RST_IWDG;
    else if (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET) cause = BM_RST_WWDG;

    s_reset_cause = cause;
    RCC_ClearFlag();

    s_boot_ticks = time_ticks32();
}

void bm_diag_note_registered(void)
{
    s_reg_ever      = true;
    s_last_reg_tick = time_ticks32();
    if (s_reg_count < 0xFFFFu) ++s_reg_count;
}

void bm_diag_loop_tick(void)
{
    const uint32_t now = time_ticks32();

    if (s_loop_prev_ok)
    {
        const uint32_t tpu = time_hw_tpus;
        if (tpu)
        {
            uint32_t us = time_diff_u32(now, s_loop_prev) / tpu;
            if (us > 0xFFFFu) us = 0xFFFFu;

            s_loop_us = s_loop_us
                ? (uint16_t)(((uint32_t)s_loop_us * 7u + us) / 8u)
                : (uint16_t)us;

            if ((uint16_t)us > s_loop_us_max) s_loop_us_max = (uint16_t)us;
        }
    }

    s_loop_prev    = now;
    s_loop_prev_ok = true;
}

static uint8_t diag_sys_flags(void)
{
    uint8_t f = 0u;

    if (bambubus_registered()) f |= 0x01u;   // bit0 已完成注册
    if (vendor_mute_pending()) f |= 0x02u;   // bit1 静默中

    for (uint8_t i = 0; i < 4u; ++i)
    {
        if (filament_channel_inserted[i])    // bit2 有通道插线
        {
            f |= 0x04u;
            break;
        }
    }

#if BMCU_DM_TWO_MICROSWITCH
    f |= 0x08u;                              // bit3 自动进料（双微动）已启用
#endif

    return f;
}

extern unsigned char g_serial_packet[];   // [0]=长度15，[1..15]=ASCII 序列号

void bm_diag_sys_get(bm_diag_sys_t *out)
{
    if (!out) return;

    memset(out, 0, sizeof(*out));

    uint32_t mean100 = ((uint32_t)s_loop_us + 50u) / 100u;
    if (mean100 > 255u) mean100 = 255u;

    out->ver           = BM_DIAG_VER;
    out->slot          = bambubus_ams_num();
    out->sys_flags     = diag_sys_flags();
    out->reset_cause   = s_reset_cause;
    out->uptime_s      = diag_since_s(s_boot_ticks);
    out->reg_count     = s_reg_count;
    out->last_reg_s    = s_reg_ever ? sat_u16(diag_since_s(s_last_reg_tick)) : BM_DIAG_LAST_SECS;
    out->fw_major      = (uint8_t)SG_VER_MAJOR;
    out->fw_year       = (uint8_t)SG_VER_YEAR;
    out->fw_week       = (uint8_t)SG_VER_WEEK;
    out->fw_build      = (uint8_t)SG_VER_BUILD;
    out->reserved      = 0u;
    out->loop_us_max   = s_loop_us_max;
    out->loop_us_100us = (uint8_t)mean100;

    out->variant = seq_no_variant_bits();
    for (uint8_t i = 0; i < 15u; ++i) out->serial[i] = (char)g_serial_packet[1u + i];

    s_loop_us_max = 0u;   // 「自上次查询以来」的语义：取完即清
}

bool bm_diag_channel_get(uint8_t ch, bm_diag_ch_t *out)
{
    if (!out || ch >= 4u) return false;
    mc_diag_channel(ch, out);
    return true;
}
