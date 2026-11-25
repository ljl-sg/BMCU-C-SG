#pragma once
#include <stdint.h>

// ==================== 自定义静默指令（0x04A0）状态机 ====================
// 说明：0x04A0 是本项目自定义的扩展指令（不是打印机厂商协议的一部分），
//       打印机不认识它；只有装了 BMCU 固件的设备才会响应。
// 流程：收到指令 → 回 ACK → 200ms 后开始静默 → 静默结束由调用方软重启 MCU
// 本模块只负责"静默窗口"的时间判断；指令解析、ACK、重启都留在协议层。

typedef enum
{
    VENDOR_MUTE_NONE = 0,     // 没有静默任务
    VENDOR_MUTE_ACTIVE,       // 静默窗口内（调用方应清空总线缓冲并跳过本轮）
    VENDOR_MUTE_EXPIRED       // 静默到时（调用方负责 NVIC_SystemReset）
} vendor_mute_state_t;

// 启动一次静默（ms 已由调用方做上下限裁剪）
void vendor_mute_arm(uint16_t ms);

// 主循环调用，返回当前静默状态（见上面枚举）
vendor_mute_state_t vendor_mute_poll(void);

// 是否已安排静默（诊断用）
bool vendor_mute_pending(void);

