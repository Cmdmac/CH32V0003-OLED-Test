/********************************** (C) COPYRIGHT *********************************
 * File Name          : ssd1306.c
 * Description        : SSD1306 / SSD1312 OLED 驱动
 *                      分辨率与初始化参数由 hub_config.h 的 HUB_OLED_TYPE 决定:
 *                        0.91 寸 128x32 SSD1306 / 0.96 寸 128x64 SSD1312(或1306)
 *                      I2C 底层在 i2c_bus.c(与 INA219 共用), 由 hub_config.h
 *                      的 HUB_I2C_USE_BITBANG 选择:
 *                        =0 硬件 I2C1(PC1=SDA, PC2=SCL, WCH EVT 标准库)
 *                        =1 软件 I2C bit-bang(同引脚, 软件逐位, 时序宽松,
 *                           用于与硬件 I2C 交叉定位问题)
 *
 * @Note   接线:
 *           CH32V003         OLED 模组
 *           PC2  (SCL)     -> SCL  (接 2.2k~4.7k 上拉至 3.3V)
 *           PC1  (SDA)     -> SDA  (接 2.2k~4.7k 上拉至 3.3V)
 *           3.3V            -> VCC
 *           GND             -> GND
 *
 * @Note   128x64 显存为 1024 字节(CH32V003 共 2KB SRAM, 属可用但偏紧);
 *         整屏刷新在 100kHz I2C 下约 90~100ms, 主循环 HUB_POLL_MS 需留意。
 **********************************************************************************/
#include "ssd1306.h"
#include "hub_config.h"
#include "i2c_bus.h"
#include "font8x16.h"

#include <string.h>

static uint8_t  s_oled_fb[SSD1306_FB_SIZE];   /* 本地显存(列x页 -> 垂直字节): 512B / 1024B */
static uint8_t  s_addr = HUB_I2C_ADDR;        /* 运行期可切换(见 ssd1306_set_addr) */

/*==========================================================================
 * I2C 底层收发: 全部委托给 i2c_bus 公共总线层(OLED/INA219 共用)
 *========================================================================*/

/* 写一帧(地址为 s_addr), 保留原函数名, 错误计数由总线层统一维护 */
static uint8_t oled_i2c_write(const uint8_t *buf, uint16_t n)
{
    return i2c_bus_write(s_addr, buf, n);
}

static void ssd1306_i2c_init(void)
{
    i2c_bus_init();
}

uint8_t ssd1306_is_addr_present(uint8_t addr7)
{
    return i2c_bus_probe(addr7);
}

/*==========================================================================
 * 延时: 公共接口保留(诊断代码/主循环在用), 实现在 i2c_bus.c
 *========================================================================*/
void ssd1306_delay_us(uint32_t us)
{
    i2c_bus_delay_us(us);
}

void ssd1306_delay_ms(uint32_t ms)
{
    i2c_bus_delay_ms(ms);
}

/*==========================================================================
 * 帧封装(两种模式共用): 命令帧/数据帧
 *========================================================================*/

/* 命令帧: 控制字节 0x00 + 命令序列 (单帧命令最多 31 条, 超长需自行分段)
 * 公开给测试程序: 现场替换初始化参数用(见 ssd1306.h)
 * @return 1=成功, 0=失败(地址/字节无 ACK) */
uint8_t ssd1306_send_cmds(const uint8_t *cmds, uint8_t n)
{
    uint8_t buf[32];

    if (n > sizeof(buf) - 1) {
        n = sizeof(buf) - 1;
    }
    buf[0] = 0x00;               /* SSD1306: 后续字节均为命令 */
    memcpy(&buf[1], cmds, n);
    return oled_i2c_write(buf, (uint16_t)(n + 1));
}

/* 数据帧: 控制字节 0x40 + 显存数据, 按 32 字节分帧防止单帧过长。
 *
 * !!! 任何一字节失败必须立即中止并返回 0, 绝不能接着发后面的分片:
 *     失败时从机只收下了前一段, 若继续把剩余分片发下去, 从机 DDRAM
 *     会少若干字节 -> 整帧后面的内容整体前移 -> 屏幕上表现为
 *     "上半屏正常、下半屏错位/错乱"(本项目出现过的那个现象)。
 * @return 1=整帧送达, 0=中途失败(调用方应重设窗口后整帧重发) */
static uint8_t oled_send_data(const uint8_t *data, uint16_t len)
{
    uint8_t buf[33];
    uint16_t chunk;

    while (len) {
        chunk = (len > 32) ? 32 : len;
        buf[0] = 0x40;           /* SSD1306: 后续字节均为显存数据 */
        memcpy(&buf[1], data, chunk);
        if (!oled_i2c_write(buf, (uint16_t)(chunk + 1))) {
            return 0;            /* 中止: 由外层重设寻址窗口后整帧重发 */
        }
        data += chunk;
        len  -= chunk;
    }
    return 1;
}

/* 同上, 但开放给测试程序: SH1106 用页寻址时必须手动"设页 + 灌一页 128 字节",
 * 不能走 ssd1306_flush_pages()(那条是水平寻址 21h/22h, SH1106 不认) */
uint8_t ssd1306_send_data(const uint8_t *data, uint16_t len)
{
    return oled_send_data(data, len);
}


/*==========================================================================
 * SSD1306 控制器操作
 *========================================================================*/

void ssd1306_init(void)
{
    /* 初始化命令两处随型号变化, 其余为 SSD1306/SSD1312 共用:
     *   0xAD IREF 选择   : SSD1312 独有! SSD1312 复位默认走"外部 IREF",
     *                      模组上不装 IREF 电阻时必须发 0xAD,0x50 切到内部
     *                      IREF, 否则段驱动器没有参考电流 -> 面板全黑而
     *                      I2C 侧完全正常(最容易误判成"总线/接线问题")。
     *   0xA8 复用比      : 32 -> 0x1F, 64 -> 0x3F (由 SSD1306_H 自动算)
     *   0x8D 电荷泵      : SSD1306 0x14 / SSD1312 0x72(9V, 只认 12/52/72/92h)
     *   0xDA SEG/COM 配置: 128x32 0x02 / 128x64-1306 0x12 / SSD1312 0x10
     *   0xD9 预充电      : SSD1306 0xF1 / SSD1312 0x22
     *   0xDB VCOMH       : SSD1306 0x40 / SSD1312 0x30
     *   0x81 对比度      : 见 hub_config.h
     * 以上取值均来自 hub_config.h 的型号预设。 */
    const uint8_t init_cmds[] = {
        0xAE,                            /* 显示关闭 */
#if (HUB_SSD1306_IREF != 0)
        0xAD, HUB_SSD1306_IREF,          /* SSD1312: IREF 选择(漏发 => 面板全黑) */
#endif
        0xD5, 0x80,                      /* 时钟分频 / 振荡频率 */
        0xA8, (uint8_t)(SSD1306_H - 1),  /* 复用比: 32->0x1F, 64->0x3F */
        0xD3, 0x00,                      /* 显示偏移 0 */
        0x40,                            /* 起始行 0 */
        0x8D, HUB_SSD1306_CP,            /* 电荷泵使能(型号相关) */
        0x20, 0x00,                      /* 水平寻址模式 */
        HUB_SSD1306_SEG,                 /* 段重映射(型号相关, 影响左右镜像) */
        HUB_SSD1306_COM,                 /* COM 扫描方向(影响上下颠倒) */
        0xDA, HUB_SSD1306_COMCFG,        /* SEG/COM 引脚配置(型号相关) */
        0x81, HUB_SSD1306_CONTRAST,      /* 对比度 */
        0xD9, HUB_SSD1306_PRECHG,        /* 预充电周期(型号相关) */
        0xDB, HUB_SSD1306_VCOMH,         /* VCOMH 电平(型号相关) */
        0xA4,                            /* 从 RAM 恢复显示 */
        0xA6,                            /* 正常显示(非反色) */
        0xAF                             /* 显示开启 */
    };

    ssd1306_i2c_init();
    ssd1306_delay_ms(100);        /* 上电等待屏幕内部稳定 */

    ssd1306_send_cmds(init_cmds, sizeof(init_cmds));
    ssd1306_clear();
    ssd1306_update();
}

/*==========================================================================
 * 本地显存操作 (布局: fb[page*128 + col], 1 字节 = 该列 8 个垂直像素)
 *========================================================================*/
void ssd1306_clear(void)
{
    memset(s_oled_fb, 0, sizeof(s_oled_fb));
}

void ssd1306_draw_char(uint8_t x, uint8_t y, char ch)
{
    uint8_t c, ptop;

    /* 只支持页对齐(8 的倍数)行坐标绘制, 否则直接忽略 */
    if ((y & 0x07) || y > SSD1306_H - FONT8X16_H) {
        return;
    }
    if (x > SSD1306_W - FONT8X16_W) {
        return;
    }
    if (ch < FONT8X16_FIRST || ch >= FONT8X16_FIRST + FONT8X16_COUNT) {
        ch = ' ';               /* 越界字符按空格处理 */
    }

    ptop = y >> 3;              /* 上半页号 (0 或 2) */

    /* 字库列序两段式: byte[c]=上半页列c, byte[8+c]=下半页列c */
    for (c = 0; c < 8; c++) {
        s_oled_fb[(ptop)     * SSD1306_W + x + c] = font8x16[ch - FONT8X16_FIRST][c];
        s_oled_fb[(ptop + 1) * SSD1306_W + x + c] = font8x16[ch - FONT8X16_FIRST][8 + c];
    }
}

void ssd1306_draw_text(uint8_t x, uint8_t y, const char *s)
{
    while (*s && x + FONT8X16_W <= SSD1306_W) {
        ssd1306_draw_char(x, y, *s);
        s++;
        x += FONT8X16_W;
    }
}

uint8_t ssd1306_update(void)
{
    return ssd1306_flush_pages((uint8_t)SSD1306_PAGES);
}

/* 把前 pages 页显存推上屏(测试程序要按运行期分辨率 4 页/8 页切换)
 *
 * 重试机制(抗"半屏错乱"的关键):
 *   传输中途失败时, 重新发一遍水平寻址命令(21h 列范围 / 22h 页范围)再整帧
 *   重发。重设窗口会把从机的 DDRAM 指针拉回 (0,0), 所以重发得到的是完整
 *   正确的一帧 —— 否则坏帧会一直留在屏上(数值不变时主循环还会跳过刷新)。
 *   失败判据由总线层的 ACK 检测给出, 重试用尽才返回 0。
 * @return 1 = 整帧送达, 0 = 重试后仍失败(调用方不要提交"已显示"标记) */
uint8_t ssd1306_flush_pages(uint8_t pages)
{
    uint8_t cmds[6];
    uint8_t try_n;

    if (pages == 0 || pages > SSD1306_PAGES) {
        pages = (uint8_t)SSD1306_PAGES;
    }

    for (try_n = 0; try_n <= HUB_OLED_FLUSH_RETRY; try_n++) {
        /* 水平寻址: 列 0..127, 页 0..(pages-1), 然后顺序灌显存 */
        cmds[0] = 0x21; cmds[1] = 0;    cmds[2] = SSD1306_W - 1;
        cmds[3] = 0x22; cmds[4] = 0;    cmds[5] = (uint8_t)(pages - 1);
        if (!ssd1306_send_cmds(cmds, (uint8_t)sizeof(cmds))) {
            continue;                /* 命令本身没送出去: 直接下一轮重试 */
        }
        if (oled_send_data(s_oled_fb, (uint16_t)SSD1306_W * pages)) {
            return 1;                /* 整帧送达 */
        }
        /* 数据中途失败: 循环回去重设寻址窗口再整帧重发 */
    }
    return 0;
}

/*==========================================================================
 * 调试/自检接口
 *========================================================================*/

uint8_t ssd1306_probe(void)
{
    return ssd1306_is_addr_present(s_addr);
}

/*---- 运行期地址切换 + 底层测试接口 ----*/
void ssd1306_set_addr(uint8_t addr7)
{
    s_addr = addr7;
}

uint8_t ssd1306_get_addr(void)
{
    return s_addr;
}

uint8_t *ssd1306_framebuffer(void)
{
    return s_oled_fb;
}

void ssd1306_fill(uint8_t val)
{
    memset(s_oled_fb, val, sizeof(s_oled_fb));
}

uint16_t ssd1306_get_err_count(void)
{
    return i2c_bus_get_err_count();     /* 总线级错误(OLED+INA219 全算) */
}

void ssd1306_clear_err_count(void)
{
    i2c_bus_clear_err_count();
}

/* 0xA5: 忽略显存全屏点亮; 0xA4: 恢复按显存显示 */
void ssd1306_set_all_on(uint8_t on)
{
    uint8_t cmd = on ? 0xA5 : 0xA4;

    ssd1306_send_cmds(&cmd, 1);
}
