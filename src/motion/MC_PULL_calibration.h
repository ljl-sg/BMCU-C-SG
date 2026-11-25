#pragma once

void mc_pull_calibration_boot();
bool mc_pull_calibration_clear();
#include <stdint.h>
bool mc_pull_calibration_is_valid(uint8_t ch);
