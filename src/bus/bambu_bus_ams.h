#pragma once
#include <stdint.h>
#include <stdbool.h>

enum class bambubus_package_type
{
    error = -1,
    none = 0,
    filament_motion_short,
    filament_motion_long,
    online_detect,
    REQx6,
    NFC_detect,
    set_filament_info,
    mc_online,
    read_filament_info,
    set_filament_info_type2,
    version,
    serial_number,
    heartbeat,
    vendor_mute,        // 0x04A0 XMCU 下发的静默指令
    diag_req,           // 0x04A1 诊断请求（哨兵 → BMCU）
    ETC,

    __BambuBus_package_packet_type_size
};

void bambubus_init(void);
void bambubus_heartbeat_seen_fast(void);
extern bambubus_package_type bambubus_run();
uint8_t bambubus_ams_num(void);
uint8_t bambubus_local_ams_index(void);
bool bambubus_registered(void);
bool bambubus_arrange_wait(void);
