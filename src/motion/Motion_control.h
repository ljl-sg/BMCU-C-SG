#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ams_addressing.h"

void motion_control_init();
void motion_control_set_pwm(uint8_t CHx, int PWM);
void motion_control_run(int error);
bool motion_control_save_dm_key_none_thresholds(void);

void mc_pull_detect_channels_inserted();

// Externy
extern float   mc_pull_v_offset[4];
extern float   mc_pull_v_min[4];
extern float   mc_pull_v_max[4];
extern uint8_t mc_pull_pct[4];
extern int8_t  mc_pull_polarity[4];
extern float   mc_dm_key_none_thresh[4];
extern bool    filament_channel_inserted[4];

// platformio.ini: -DAMS_RETRACT_LEN
#ifndef AMS_RETRACT_LEN
#define AMS_RETRACT_LEN 0.2f
#endif

// platformio.ini: -DBMCU_DM_TWO_MICROSWITCH=1 (DM dual microswitch + autoload assist)
#ifndef BMCU_DM_TWO_MICROSWITCH
#define BMCU_DM_TWO_MICROSWITCH 0
#endif

// platformio.ini: -DBMCU_ONLINE_LED_FILAMENT_RGB=1 (show filament RGB on ONLINE LED when loaded)
#ifndef BMCU_ONLINE_LED_FILAMENT_RGB
#define BMCU_ONLINE_LED_FILAMENT_RGB 0
#endif

#ifndef motion_control_ams_num
#define motion_control_ams_num BMCU_LOCAL_AMS_INDEX
#endif

#ifndef motion_control_pull_back_distance
#define motion_control_pull_back_distance AMS_RETRACT_LEN
#endif

// 诊断用只读访问器（0x04A1/0x04A2，实现在 Motion_control.cpp，类型见 app_api.h）
struct bm_diag_ch_s;
void mc_diag_channel(uint8_t ch, struct bm_diag_ch_s *out);

bool motion_control_filament_present(uint8_t ch);
