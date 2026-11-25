#include "vendor_mute.h"

#include "hal/time_hw.h"      // time_ticks32 / ms_to_ticks32

// 静默状态
static bool     s_pending = false;
static uint32_t s_start   = 0u;   // 静默开始时刻（留 200ms 给 ACK 发出去）
static uint32_t s_until   = 0u;   // 静默结束时刻

void vendor_mute_arm(uint16_t ms)
{
    const uint32_t now = time_ticks32();
    s_start   = now + ms_to_ticks32(200u);
    s_until   = s_start + ms_to_ticks32((uint32_t)ms);
    s_pending = true;
}

bool vendor_mute_pending(void)
{
    return s_pending;
}

vendor_mute_state_t vendor_mute_poll(void)
{
    if (!s_pending)
        return VENDOR_MUTE_NONE;

    const uint32_t n32 = time_ticks32();

    // 还没到静默点：正常收发（这段时间用来把 ACK 发出去）
    if ((int32_t)(n32 - s_start) < 0)
        return VENDOR_MUTE_NONE;

    // 静默窗口内
    if ((int32_t)(n32 - s_until) < 0)
        return VENDOR_MUTE_ACTIVE;

    // 静默到时：交回调用方处理（原实现即在协议层调用 NVIC_SystemReset）
    s_pending = false;
    return VENDOR_MUTE_EXPIRED;
}
