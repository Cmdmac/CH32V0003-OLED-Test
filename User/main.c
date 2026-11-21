/********************************** (C) COPYRIGHT *********************************
 * File Name          : main.c
 * Description        : OLED 点亮测试 (SSD1312 / SSD1306)
 *                      从 USB-HUB 项目精简而来, 只保留屏幕驱动逻辑。
 *
 * 硬件: CH32V003 + OLED (I2C, 软件 bit-bang)
 *       PA2 = SCL, PA1 = SDA (均开漏, 靠外部上拉)
 *       3.3V -> VCC, GND -> GND
 *
 * 测试流程:
 *   1) 上电 -> 初始化屏幕(含清屏)
 *   2) 全屏点亮 0.6s (验证 I2C 与屏幕硬件)
 *   3) 清屏 -> 显示 4 行测试文字
 *   4) 主循环每 2s 切换一次显示(翻转全屏), 证明程序活着
 **********************************************************************************/
#include "debug.h"

#include "hub_config.h"
#include "i2c_bus.h"
#include "ssd1306.h"

/* 主循环里全屏闪烁的节奏(ms) */
#define TEST_BLINK_MS   2000

int main(void)
{
    uint8_t blink_on = 0;

    SystemCoreClockUpdate();          /* 更新全局内核时钟变量(延时依赖) */
    Delay_Init();                     /* EVT 标准 SysTick 延时初始化 */

    /* 1) 初始化屏幕: I2C 总线 + 控制器配置 + 清屏(内部含上电延时) */
    ssd1306_init();

    /* 2) 全屏点亮 0.6s: 验证 I2C 与屏幕硬件 OK */
    ssd1306_set_all_on(1);
    Delay_Ms(600);
    ssd1306_set_all_on(0);
    Delay_Ms(200);

    /* 3) 显示测试文字 */
    ssd1306_clear();
    ssd1306_draw_text(0, 0,  "OLED TEST OK");
    ssd1306_draw_text(0, 16, "SSD1312 128x64");
    ssd1306_draw_text(0, 32, "SCL=PA2 SDA=PA1");
    ssd1306_draw_text(0, 48, "I2C bit-bang");
    ssd1306_update();

    /* 4) 主循环: 每 2s 全屏翻转一次, 证明程序持续运行 */
    while (1) {
        Delay_Ms(TEST_BLINK_MS);
        blink_on = !blink_on;
        ssd1306_set_all_on(blink_on);
    }
}
