#pragma once
#include <stdint.h>

// 构造 AMS 序列号（写入 packet[0..15] 与显示字段 packet[33..40]）
//   ams_num : 0~3（A~D 槽）；is_auto 为 true 时表示自动注册（AUTO）
//   packet  : 至少 66 字节的版本/序列号报文缓冲区
void seq_no_build(uint8_t *packet, uint8_t ams_num, bool is_auto);

// 变体位（诊断上报用）：bit1:0 档位、bit3:2 退料、bit4 双微动、bit5 耗材灯、bit6 AUTO
uint8_t seq_no_variant_bits(void);
