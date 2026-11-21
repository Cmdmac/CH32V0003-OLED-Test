/********************************** (C) COPYRIGHT *********************************
 * File Name          : ssd1306.h
 * Description        : SSD1306 / SSD1312 OLED (I2C, CH32V003 硬件 I2C1) 驱动头文件
 *                      分辨率由 hub_config.h 的 HUB_OLED_TYPE 决定:
 *                        0.91 寸 128x32 (SSD1306) / 0.96 寸 128x64 (SSD1312/1306)
 **********************************************************************************/
#ifndef __SSD1306_H
#define __SSD1306_H

#include <stdint.h>
#include "hub_config.h"

#define SSD1306_W           128      /* 宽 128 像素 */
#define SSD1306_H           HUB_OLED_H                  /* 高 32 / 64, 见 hub_config.h */
#define SSD1306_PAGES       (SSD1306_H / 8)             /* 页数 4 / 8 */
#define SSD1306_FB_SIZE     (SSD1306_W * SSD1306_PAGES) /* 显存 512B / 1024B */

/*
 * 初始化 I2C1(PC1=SDA, PC2=SCL) 与屏幕控制器。
 * 初始化命令按 hub_config.h 的型号预设自动选择(SSD1306 / SSD1312)。
 * 上电后需等待屏幕稳定(内部延时约 100ms), 会顺带清屏。
 */
void ssd1306_init(void);

/* 清空本地显存(不立即上屏, 需再调 ssd1306_update) */
void ssd1306_clear(void);

/*
 * 画一个 8x16 字符到本地显存。
 * @param x  字符左上角列(0..120)
 * @param y  字符左上角行, 必须是 8 的倍数(本驱动只支持页对齐绘制):
 *           128x32 上第 1 行文字用 y=0, 第 2 行用 y=16
 *           128x64 上可用 y=0 / 16 / 32 / 48, 共 4 行
 * @param ch 0x20~0x7E 内可打印字符, 越界字符按空格处理
 */
void ssd1306_draw_char(uint8_t x, uint8_t y, char ch);

/* 按顺序绘制一串字符(每字符 8px 宽), 超宽自动截断 */
void ssd1306_draw_text(uint8_t x, uint8_t y, const char *s);

/*
 * 把本地显存整屏推送到屏幕(内部自动按 32 字节分帧 + 失败自动重发)。
 * @return 1 = 整帧送达; 0 = 重试后仍失败
 *         (调用方应保留"脏"状态, 下一轮再画 —— 否则坏帧会一直挂在屏上)
 */
uint8_t ssd1306_update(void);

/*
 * 只推送前 pages 页显存(1 页 = 8 行)。
 * 正常运行用 ssd1306_update() 即可; 测试程序要用一个固件跑不同分辨率
 * (32 行=4 页 / 64 行=8 页), 才需要按运行期值指定页数。
 * 传输失败会自动重设寻址窗口并整帧重发(次数见 HUB_OLED_FLUSH_RETRY)。
 */
uint8_t ssd1306_flush_pages(uint8_t pages);

/* 毫秒/微秒阻塞延时 (SysTick 方案, 依赖 SystemCoreClock) */
void ssd1306_delay_ms(uint32_t ms);
void ssd1306_delay_us(uint32_t us);

/*==========================================================================
 * 调试/自检接口(不依赖串口, 应用层拿返回值自己打印)
 *========================================================================*/

/*
 * 只发地址看有没有 ACK(不发数据), 用来判断"这一路 I2C 上有没有这个设备"。
 * @param addr7 7 位地址(如 0x3C)
 * @return 1 = 有设备应答, 0 = 无 ACK(没接 / 地址错 / 上拉或接线问题)
 */
uint8_t ssd1306_is_addr_present(uint8_t addr7);

/* 用 hub_config.h 里配置的 HUB_I2C_ADDR 探测屏幕是否存在 */
uint8_t ssd1306_probe(void);

/*
 * 累计 I2C 传输失败次数(地址无 ACK 或字节传输超时)。
 * 初始化 + 刷屏后读一次: 一直是 0 说明这条总线通讯正常。
 */
uint16_t ssd1306_get_err_count(void);
void     ssd1306_clear_err_count(void);

/*
 * 整屏全亮自检(SSD1306 的 0xA5 命令, 忽略显存直接点亮全部像素)。
 * 用来区分"I2C 通不通"和"字库/排版对不对":
 *   全亮 => I2C 与屏幕硬件 OK, 问题在软件; 不亮 => 先查硬件。
 */
void ssd1306_set_all_on(uint8_t on);

/*==========================================================================
 * 底层接口(屏幕点亮测试程序用, 正常运行不需要)
 *========================================================================*/

/*
 * 直接发一串控制器命令(控制字节 0x00 由本函数补)。
 * 初始化序列在 ssd1306_init() 里是写死的, 测试程序需要现场换参数,
 * 所以把这条通路开放出来。n 上限 31。
 * @return 1=成功, 0=失败
 */
uint8_t ssd1306_send_cmds(const uint8_t *cmds, uint8_t n);

/*
 * 直接发一串显存数据(控制字节 0x40 由本函数补), 内部按 32 字节自动分帧。
 * 与 ssd1306_flush_pages() 的区别: 后者会先发水平寻址命令(21h/22h),
 * SH1106 不认这两条, 必须自己发页寻址(B0h+00h/10h)再调本函数灌一页。
 * @return 1=整帧送达, 0=中途失败
 */
uint8_t ssd1306_send_data(const uint8_t *data, uint16_t len);

/* 取本地显存指针(SSD1306_FB_SIZE 字节), 测试程序可直接填图案后 update */
uint8_t *ssd1306_framebuffer(void);

/* 显存整片填同一字节: 0x00 = 全灭, 0xFF = 全亮 */
void ssd1306_fill(uint8_t val);

/*
 * 运行期切换屏幕 I2C 地址(默认取 hub_config.h 的 HUB_I2C_ADDR)。
 * 模块的 SA0 焊盘决定 0x3C/0x3D, 拆焊前用测试程序两个都试一遍最快。
 */
void    ssd1306_set_addr(uint8_t addr7);
uint8_t ssd1306_get_addr(void);

#endif /* __SSD1306_H */
