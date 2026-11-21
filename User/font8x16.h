/********************************** (C) COPYRIGHT *********************************
 * File Name          : font8x16.h
 * Description        : ASCII 8x16 点阵字库声明 (0x20..0x7E, 95 字符)
 *
 * 字库来源: ssd1306xled_font8x16 (Neven Boyanov / Tinusaur Team),
 *          经 lexus2k/ssd1306(MIT) 分发, 本工程仅作像素数据裁剪使用。
 * 存储格式: 每字符 16 字节, 列序两段式 ---
 *           byte[c]      = 第 c 列, 上半 8 行 (bit0 为该页顶部)
 *           byte[8 + c]  = 第 c 列, 下半 8 行 (bit0 为该页顶部)
 *           直接对应 SSD1306 页(8像素高)数据字节, 无需转置。
 **********************************************************************************/
#ifndef __FONT8X16_H
#define __FONT8X16_H

#include <stdint.h>

#define FONT8X16_W       8          /* 字符宽(像素) */
#define FONT8X16_H       16         /* 字符高(像素) */
#define FONT8X16_FIRST   0x20       /* 字库起始 ASCII 码: 空格 */
#define FONT8X16_COUNT   95         /* 0x20 ~ 0x7E */

extern const uint8_t font8x16[FONT8X16_COUNT][FONT8X16_H];

#endif /* __FONT8X16_H */
