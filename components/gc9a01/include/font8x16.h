/**
 * @file font8x16.h
 * @brief 8x16 ASCII 点阵字模声明。
 */

#pragma once

#include <stdint.h>

/**
 * @brief ASCII 0x20..0x7E 的 8x16 点阵字模。
 *
 * 第一维索引为字符值减去 0x20；第二维为从上至下的行号。
 * 每个字节的 bit7 表示最左侧像素，bit0 表示最右侧像素。
 */
extern const uint8_t font8x16[95][16];
