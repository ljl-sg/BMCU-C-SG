#include "seq_no.h"
#include "ams_addressing.h"

// ===================== AMS 序列号构造 =====================
// 序列号 15 位：B0C <UID 7 位> <槽位><自动进料><档位><退料长度><耗材灯>
// 字母位只能是十六进制字符（打印机把显示字段按十六进制渲染）
//
// 数据同时写两处：
//   packet[1..15]  ASCII 序列号（0x0402 应答）
//   packet[33..40] 显示字段（8 字节 = 16 nibble，打印机 AMS 页面显示）
//
// 前缀 B0C 是 BMCU 开源固件标识；配置段放最后 5 位，便于只报最后 5 位。

static inline uint8_t bmcu_tier_code(void)
{
#if defined(BMCU_P1S) && BMCU_P1S
    return 2u;
#elif defined(BMCU_SOFT_LOAD) && BMCU_SOFT_LOAD
    return 0u;
#else
    return 1u;
#endif
}

static inline uint8_t bmcu_retract_code(void)
{
#ifdef AMS_RETRACT_LEN
    constexpr float r = AMS_RETRACT_LEN;
    if (r < 0.35f) return 0u;
    if (r < 0.60f) return 1u;
    if (r < 0.80f) return 2u;
    return 3u;
#else
    return 1u;
#endif
}

// ---- 序列号相关常量（与本文件绑定，避免依赖协议层内部常量）----
static constexpr uint8_t  SERIAL_LEN      = 15;   // ASCII 序列号字符数
static constexpr uint8_t  SERIAL_DISP_OFF = 33;   // 显示字段在报文中的偏移
static constexpr uint8_t  SERIAL_DISP_LEN = 8;    // 显示字段字节数（8 字节 = 16 nibble）
static constexpr uint32_t MCU_UID_ADDR    = 0x1FFFF7E8u;
static constexpr uint8_t  MCU_UID_BYTES   = 12;   // CH32V203 唯一 ID 96 位

void seq_no_build(uint8_t *packet, uint8_t ams_num, bool is_auto)
{
    static const char hex[] = "0123456789ABCDEF";
    volatile const uint8_t *uid = (volatile const uint8_t *)MCU_UID_ADDR;

    uint64_t v = 1469598103934665603ull;
    for (int i = 0; i < MCU_UID_BYTES; i++)
    {
        v ^= uid[i];
        v *= 1099511628211ull;
    }
    v ^= v >> 30;
    v *= 0xBF58476D1CE4E5B9ull;
    v ^= v >> 27;
    v *= 0x94D049BB133111EBull;
    v ^= v >> 31;

    // 序列号 15 位：
    //   [1..3]   BMCU 标识 B0C（"BMCU" 里的 M/U 不是十六进制字符，用 0 代替 O）
    //   [4..10]  UID 7 位（28 位，2.68 亿种，同机多台不重号）
    //   [11]     槽位：A~D = A~D 槽，0 = 自动注册(AUTO)
    //   [12]     自动进料：1 = 开，0 = 关
    //   [13]     档位：1 = soft_load，2 = standard，3 = high_force_load
    //   [14]     退料长度：0 = 0.20f，1 = 0.50f，2 = 0.70f，3 = 0.90f
    //   [15]     耗材灯：1 = 开，0 = 关
    // 配置段放在最后 5 位：别人问"这序列号什么意思"时，只需报最后 5 位。
    packet[0] = SERIAL_LEN;
    packet[1] = 'B';
    packet[2] = '0';
    packet[3] = 'C';

    {
        const uint8_t b0 = (uint8_t)(v >> 56);
        const uint8_t b1 = (uint8_t)(v >> 48);
        const uint8_t b2 = (uint8_t)(v >> 40);
        const uint8_t b3 = (uint8_t)(v >> 32);
        packet[4]  = hex[(b0 >> 4) & 0x0F];
        packet[5]  = hex[b0 & 0x0F];
        packet[6]  = hex[(b1 >> 4) & 0x0F];
        packet[7]  = hex[b1 & 0x0F];
        packet[8]  = hex[(b2 >> 4) & 0x0F];
        packet[9]  = hex[b2 & 0x0F];
        packet[10] = hex[(b3 >> 4) & 0x0F];
    }

    packet[11] = (uint8_t)(is_auto ? '0' : (uint8_t)('A' + ams_num));

#if defined(BMCU_DM_TWO_MICROSWITCH) && BMCU_DM_TWO_MICROSWITCH
    packet[12] = '1';
#else
    packet[12] = '0';
#endif

    packet[13] = (uint8_t)('1' + bmcu_tier_code());
    packet[14] = (uint8_t)('0' + bmcu_retract_code());

#if defined(BMCU_ONLINE_LED_FILAMENT_RGB) && BMCU_ONLINE_LED_FILAMENT_RGB
    packet[15] = '1';
#else
    packet[15] = '0';
#endif

#ifdef BMCU_SERIAL_TAG_UID
    {
        const char *tag = BMCU_SERIAL_TAG_UID;
        for (uint8_t i = 0; i < 7u && tag[i]; ++i)
            packet[4u + i] = (uint8_t)tag[i];
    }
#endif

    // 显示字段：[33..40] 这 8 字节按十六进制展开，打印机显示前 15 个字符
    {
        auto nib = [](uint8_t c) -> uint8_t {
            if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
            if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
            if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
            return 0u;
        };
        for (uint8_t i = 0; i < 8u; ++i) {
            const uint8_t c0 = packet[1u + i * 2u];
            const uint8_t c1 = (uint8_t)(i * 2u + 2u <= 15u)
                               ? packet[2u + i * 2u]
                               : (uint8_t)'0';
            packet[SERIAL_DISP_OFF + i] = (uint8_t)((nib(c0) << 4) | nib(c1));
        }
    }

}

// 诊断上报用的变体位（与序列号里的配置段同源，避免两处逻辑漂移）
uint8_t seq_no_variant_bits(void)
{
    uint8_t b = 0u;
    b |= (uint8_t)(bmcu_tier_code() & 0x03u);             // bit1:0 档位
    b |= (uint8_t)((bmcu_retract_code() & 0x03u) << 2);   // bit3:2 退料
#if defined(BMCU_DM_TWO_MICROSWITCH) && BMCU_DM_TWO_MICROSWITCH
    b |= 0x10u;                                           // bit4 双微动
#endif
#if defined(BMCU_ONLINE_LED_FILAMENT_RGB) && BMCU_ONLINE_LED_FILAMENT_RGB
    b |= 0x20u;                                           // bit5 耗材灯
#endif
#if defined(BMCU_AMS_AUTO) && BMCU_AMS_AUTO
    b |= 0x40u;                                           // bit6 AUTO
#endif
    return b;
}
