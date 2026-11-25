// ============================================================================
// 文件：ams.cpp
// 职责：AMS 数据模型（全局 ams[] 的定义与初始化）
// 主要接口：ams_init / ams_datas_read
// 依赖：ams.h
// 注意：ams[] 是全局可变状态，多处直接读写，改动字段需全局搜索
// ============================================================================
#include "ams.h"
_ams ams[ams_max_number];
uint8_t bus_now_ams_num=0;

void ams_init()
{
    for(uint8_t i=0;i<ams_max_number;i++)
    {
        ams[i].init();
    }
}


