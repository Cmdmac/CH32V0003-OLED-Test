/********************************** (C) COPYRIGHT *********************************
 * File Name          : i2c_bus.c
 * Description        : CH32V003 I2C1 公共总线层实现(OLED / INA219 共用)
 *
 * @Note   接线:
 *           CH32V003         I2C 设备(SSD1306 / INA219 ...)
 *           PC2  (SCL)     -> SCL  (总线共用一组上拉 2.2k~4.7k 至 3.3V)
 *           PC1  (SDA)     -> SDA
 *           3.3V            -> VCC
 *           GND             -> GND
 **********************************************************************************/
#include "i2c_bus.h"
#include "hub_config.h"

#include "ch32v00x.h"

static uint16_t s_bus_err = 0;          /* 总线级传输失败累计 */

/*==========================================================================
 * 延时: 与 WCH EVT Debug/debug.c 同构的 SysTick 阻塞延时
 *        (青稞 V2A 的 SysTick 计数值 = SystemCoreClock/8/1000 每毫秒)
 *========================================================================*/
static uint32_t s_dly_us = 0;           /* 每微秒 SysTick 计数 */

static void bus_delay_tick_init(void)
{
    s_dly_us = SystemCoreClock / 8000000;
}

void i2c_bus_delay_us(uint32_t us)
{
    if (s_dly_us == 0) {
        bus_delay_tick_init();
    }
    SysTick->SR  &= ~(1 << 0);
    SysTick->CMP  = us * s_dly_us;
    SysTick->CNT  = 0;
    SysTick->CTLR |= (1 << 0);
    while ((SysTick->SR & (1 << 0)) != (1 << 0));
    SysTick->CTLR &= ~(1 << 0);
}

void i2c_bus_delay_ms(uint32_t ms)
{
    if (s_dly_us == 0) {
        bus_delay_tick_init();
    }
    SysTick->SR  &= ~(1 << 0);
    SysTick->CMP  = ms * s_dly_us * 1000;
    SysTick->CNT  = 0;
    SysTick->CTLR |= (1 << 0);
    while ((SysTick->SR & (1 << 0)) != (1 << 0));
    SysTick->CTLR &= ~(1 << 0);
}

/*==========================================================================
 * 模式 A: 软件 I2C(bit-bang)
 *   PA2=SCL, PA1=SDA, 均开漏(靠外部上拉), 半位周期 ~5us(约 100kHz)
 *========================================================================*/
#if HUB_I2C_USE_BITBANG

#define SW_SCL_PIN   GPIO_Pin_2
#define SW_SDA_PIN   GPIO_Pin_1

static void sw_i2c_gpio_init(void)
{
    GPIO_InitTypeDef gpio_its = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio_its.GPIO_Pin   = SW_SCL_PIN;
    gpio_its.GPIO_Mode  = GPIO_Mode_Out_OD;
    gpio_its.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOA, &gpio_its);

    gpio_its.GPIO_Pin   = SW_SDA_PIN;
    gpio_its.GPIO_Mode  = GPIO_Mode_Out_OD;
    gpio_its.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOA, &gpio_its);

    /* 空闲态两条线都释放(高电平) */
    GPIO_SetBits(GPIOA, SW_SCL_PIN | SW_SDA_PIN);
}

static void sw_scl(uint8_t v)
{
    if (v) { GPIO_SetBits(GPIOA, SW_SCL_PIN); } else { GPIO_ResetBits(GPIOA, SW_SCL_PIN); }
}

static void sw_sda(uint8_t v)
{
    if (v) { GPIO_SetBits(GPIOA, SW_SDA_PIN); } else { GPIO_ResetBits(GPIOA, SW_SDA_PIN); }
}

/* SDA 置高并读回真实电平(0=被从机拉低) */
static uint8_t sw_sda_read(void)
{
    return GPIO_ReadInputDataBit(GPIOA, SW_SDA_PIN) == SET ? 1 : 0;
}

/* SCL 读回真实电平(用于等上升沿/判断卡死) */
static uint8_t sw_scl_read(void)
{
    return GPIO_ReadInputDataBit(GPIOA, SW_SCL_PIN) == SET ? 1 : 0;
}

static void sw_half_delay(void)
{
    i2c_bus_delay_us(HUB_I2C_BITBANG_HALF_US);
}

/*--------------------------------------------------------------------------
 * 拉高 SCL 并等它"真的到高电平"(而不是固定延时后盲采样)
 *
 * 为什么需要: 开漏 + 上拉偏弱(10k) + 杜邦线/排线电容时, SCL 上升沿可能
 * 慢到几百 ns~几 us。原来固定延时 5us 就下时钟, 从机在上升沿采到的可能
 * 不是稳定的高电平 -> 偶发错位/错字, 而 ACK 检测未必报错(表现为屏幕
 * 时不时半屏错乱)。这里改成等线上电平真的过 VIH 再继续, 顺便兼容从机
 * 的时钟拉伸(SCL 被从机拉低时等它放开)。
 * 等待上限 HUB_I2C_SCL_WAIT_US: 置 0 则退化为原来的固定延时行为。
 * @return 1 = 已到高电平; 0 = 超时(SCL 被拉死: 上拉/供电/接线问题)
 *------------------------------------------------------------------------*/
static uint8_t sw_scl_high(void)
{
#if (HUB_I2C_SCL_WAIT_US > 0)
    uint8_t t;

    sw_scl(1);
    for (t = 0; t < HUB_I2C_SCL_WAIT_US; t++) {
        if (sw_scl_read()) {
            return 1;
        }
        i2c_bus_delay_us(1);
    }
    return 0;               /* 超时: 调用方继续跑, 由错误计数/恢复机制兜底 */
#else
    sw_scl(1);
    return sw_scl_read();   /* 不等待, 只回读一次(与旧版行为一致) */
#endif
}

/*--------------------------------------------------------------------------
 * 传输前确认总线空闲: 正常空闲时 SDA 被上拉拉高; 若 SDA 一直低, 说明
 * 从机在上次传输中途掉电/被打断而卡住(它会把 SDA 拉死), 此时后面每一帧
 * 都会失败 -> 屏幕半屏错乱/整体不动。先补时钟把它顶出去再开始。
 *------------------------------------------------------------------------*/
static void sw_bus_guard(void)
{
    if (!sw_sda_read()) {
        i2c_bus_recover();
    }
}

static void sw_i2c_start(void)
{
    sw_sda(1);
    sw_scl_high();
    sw_half_delay();
    sw_sda(0);              /* SCL 高电平时 SDA 下降沿 = START */
    sw_half_delay();
    sw_scl(0);
    sw_half_delay();
}

static void sw_i2c_stop(void)
{
    sw_sda(0);
    sw_scl(0);
    sw_half_delay();
    sw_scl_high();
    sw_half_delay();
    sw_sda(1);              /* SCL 高电平时 SDA 上升沿 = STOP */
    sw_half_delay();
}

/* 发送一个字节(MSB 在前), 返回 1 = 收到 ACK */
static uint8_t sw_i2c_write_byte(uint8_t b)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0; i < 8; i++) {
        sw_sda((b & 0x80) ? 1 : 0);
        b <<= 1;
        sw_half_delay();
        sw_scl_high();
        sw_half_delay();
        sw_scl(0);
        sw_half_delay();
    }

    /* 第 9 个时钟: 释放 SDA, 读从机 ACK */
    sw_sda(1);
    sw_half_delay();
    sw_scl_high();
    sw_half_delay();
    ack = sw_sda_read();    /* 0 = 从机拉低 = ACK */
    sw_scl(0);
    sw_half_delay();

    return (ack == 0) ? 1 : 0;
}

/* 接收一个字节, ack=1 回 ACK(继续读), ack=0 回 NACK(读完) */
static uint8_t sw_i2c_read_byte(uint8_t ack)
{
    uint8_t i, b = 0;

    sw_sda(1);                              /* 释放 SDA 由从机驱动 */
    for (i = 0; i < 8; i++) {
        sw_half_delay();
        sw_scl_high();
        sw_half_delay();
        b = (uint8_t)((b << 1) | sw_sda_read());
        sw_scl(0);
    }

    /* 第 9 个时钟: 主机回 ACK/NACK */
    sw_sda(ack ? 0 : 1);
    sw_half_delay();
    sw_scl_high();
    sw_half_delay();
    sw_scl(0);
    sw_sda(1);                              /* 释放 SDA */
    sw_half_delay();

    return b;
}

/* START + ADDR7|W + [wn 字节] + (Re)START + ADDR7|R + [rn 字节] + STOP */
uint8_t i2c_bus_write_read(uint8_t addr7,
                           const uint8_t *wbuf, uint16_t wn,
                           uint8_t *rbuf, uint16_t rn)
{
    uint16_t i;

    sw_bus_guard();                         /* SDA 被从机拉死时先恢复 */
    sw_i2c_start();
    if (!sw_i2c_write_byte((uint8_t)(addr7 << 1))) { goto fail; }

    for (i = 0; i < wn; i++) {
        if (!sw_i2c_write_byte(wbuf[i])) { goto fail; }
    }

    sw_i2c_start();                         /* repeated START */
    if (!sw_i2c_write_byte((uint8_t)((addr7 << 1) | 1))) { goto fail; }

    for (i = 0; i < rn; i++) {
        rbuf[i] = sw_i2c_read_byte((i < (uint16_t)(rn - 1)) ? 1 : 0);
    }
    sw_i2c_stop();
    return 1;

fail:
    s_bus_err++;
    sw_i2c_stop();
    return 0;
}

uint8_t i2c_bus_write(uint8_t addr7, const uint8_t *buf, uint16_t n)
{
    uint16_t i;

    if (n == 0) {
        return 1;
    }

    sw_bus_guard();                         /* SDA 被从机拉死时先恢复 */
    sw_i2c_start();
    if (!sw_i2c_write_byte((uint8_t)(addr7 << 1))) {
        s_bus_err++;
        sw_i2c_stop();
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (!sw_i2c_write_byte(buf[i])) {
            s_bus_err++;
            sw_i2c_stop();
            return 0;
        }
    }
    sw_i2c_stop();
    return 1;
}

void i2c_bus_init(void)
{
    sw_i2c_gpio_init();
}

/* 只发地址看有无 ACK */
uint8_t i2c_bus_probe(uint8_t addr7)
{
    uint8_t ok;

    sw_bus_guard();
    sw_i2c_start();
    ok = sw_i2c_write_byte((uint8_t)(addr7 << 1));
    sw_i2c_stop();
    return ok;
}

/*==========================================================================
 * 总线诊断(两种模式共用接口)
 *   bit-bang 下 PC1/PC2 本来就是开漏输出, 释放=外部上拉拉高,
 *   直接读输入寄存器即可得到真实线电平(经典做法)。
 *========================================================================*/
uint8_t i2c_bus_read_lines(void)
{
    uint8_t v = 0;

    sw_sda(1);              /* 释放两条线, 免得自己把它拉低 */
    sw_scl(1);
    i2c_bus_delay_us(10);

    if (sw_sda_read())                 { v |= 0x02; }   /* bit1 = SDA */
    if (GPIO_ReadInputDataBit(GPIOA, SW_SCL_PIN) == SET) { v |= 0x01; }  /* bit0 = SCL */
    return v;
}

void i2c_bus_recover(void)
{
    uint8_t i;

    sw_sda(1);
    sw_scl(1);
    i2c_bus_delay_us(10);

    /* 最多 9 个时钟: SDA 一旦被释放就停 */
    for (i = 0; i < 9; i++) {
        if (sw_sda_read()) {
            break;
        }
        sw_scl(0); i2c_bus_delay_us(5);
        sw_scl(1); i2c_bus_delay_us(5);
    }
    sw_i2c_stop();          /* 补一个规范 STOP, 让从机状态机归位 */
}

/*==========================================================================
 * 模式 B: 硬件 I2C1(事件轮询, 与 EVT I2C_EEPROM 例程同款)
 *========================================================================*/
#else  /* HUB_I2C_USE_BITBANG == 0 */

#define BUS_I2C        I2C1

/*
 * 注意: WCH 的 I2C_Send7bitAddress() 是直接把参数写进数据寄存器(bit0 当读写位),
 * 所以这里必须传"左移 1 位后的 8 位地址" —— 即 (7 位地址 << 1)。
 */
#define BUS_ADDR8(a7)   ((uint8_t)((a7) << 1))

/* 等待事件/标志的轮询上限, 防止总线悬空时死循环 */
#define I2C_POLL_LIMIT    0x7FFF   /* 数据传输阶段 */
#define I2C_PROBE_LIMIT   2000     /* 地址探测阶段: ACK 在微秒级出现,
                                    * 短超时让"无应答地址"秒过 */

/* 事件常量是 32 位(如 I2C_EVENT_MASTER_MODE_SELECT = 0x00030001),
 * 形参必须是 uint32_t —— 用 uint16_t 会被截断, 事件永远比较失败 */
static uint8_t i2c_wait_event(uint32_t ev)
{
    uint32_t t = I2C_POLL_LIMIT;
    while (t--) {
        if (I2C_CheckEvent(BUS_I2C, ev)) {
            return 1;
        }
    }
    return 0;
}

static uint8_t i2c_bus_free(void)
{
    uint32_t t = I2C_POLL_LIMIT;
    while (t--) {
        if (I2C_GetFlagStatus(BUS_I2C, I2C_FLAG_BUSY) == RESET) {
            return 1;
        }
    }
    return 0;
}

/* 发送一帧: START + [ADDR|W] + buf[0..n-1] + STOP, 成功返回 1 */
uint8_t i2c_bus_write(uint8_t addr7, const uint8_t *buf, uint16_t n)
{
    uint16_t i;

    if (!i2c_bus_free()) {
        s_bus_err++;
        return 0;
    }

    I2C_GenerateSTART(BUS_I2C, ENABLE);
    if (!i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) {
        goto fail;
    }

    I2C_Send7bitAddress(BUS_I2C, BUS_ADDR8(addr7), I2C_Direction_Transmitter);
    if (!i2c_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        /* 设备不在线上(无 ACK), 直接放弃, 不阻塞主流程 */
        goto fail;
    }

    for (i = 0; i < n; i++) {
        I2C_SendData(BUS_I2C, buf[i]);
        if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
            goto fail;
        }
    }

    I2C_GenerateSTOP(BUS_I2C, ENABLE);
    return 1;

fail:
    s_bus_err++;
    I2C_GenerateSTOP(BUS_I2C, ENABLE);
    return 0;
}

/*
 * 写寄存器地址后(Repeated START)读 rn 字节。
 * 2 字节寄存器读(INA219)时序: 收到第 1 字节 RXNE 后先关 ACK + 发 STOP,
 * 再读出两字节, 防止硬件继续灌第 3 字节。
 */
uint8_t i2c_bus_write_read(uint8_t addr7,
                           const uint8_t *wbuf, uint16_t wn,
                           uint8_t *rbuf, uint16_t rn)
{
    uint16_t i;

    if (!i2c_bus_free()) {
        s_bus_err++;
        return 0;
    }

    /* --- 写阶段: 指向目标寄存器 --- */
    I2C_GenerateSTART(BUS_I2C, ENABLE);
    if (!i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) {
        goto fail;
    }
    I2C_Send7bitAddress(BUS_I2C, BUS_ADDR8(addr7), I2C_Direction_Transmitter);
    if (!i2c_wait_event(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        goto fail;
    }
    for (i = 0; i < wn; i++) {
        I2C_SendData(BUS_I2C, wbuf[i]);
        if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
            goto fail;
        }
    }

    /* --- 读阶段: Repeated START + ADDR|R --- */
    I2C_GenerateSTART(BUS_I2C, ENABLE);
    if (!i2c_wait_event(I2C_EVENT_MASTER_MODE_SELECT)) {
        goto fail;
    }
    I2C_Send7bitAddress(BUS_I2C, BUS_ADDR8(addr7), I2C_Direction_Receiver);
    if (!i2c_wait_event(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) {
        goto fail;
    }

    for (i = 0; i < rn; i++) {
        if (i == (uint16_t)(rn - 1)) {
            /* 最后一字节: 提前 NACK + STOP, 再等 RXNE 读走 */
            I2C_AcknowledgeConfig(BUS_I2C, DISABLE);
            I2C_GenerateSTOP(BUS_I2C, ENABLE);
        }
        if (!i2c_wait_event(I2C_EVENT_MASTER_BYTE_RECEIVED)) {
            goto fail;
        }
        rbuf[i] = I2C_ReceiveData(BUS_I2C);
    }
    I2C_AcknowledgeConfig(BUS_I2C, ENABLE);     /* 恢复 ACK, 供下次传输 */

    return 1;

fail:
    s_bus_err++;
    I2C_AcknowledgeConfig(BUS_I2C, ENABLE);
    I2C_GenerateSTOP(BUS_I2C, ENABLE);
    return 0;
}

void i2c_bus_init(void)
{
    GPIO_InitTypeDef  gpio_its = {0};
    I2C_InitTypeDef   i2c_its  = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* PC2 = I2C1_SCL, PC1 = I2C1_SDA, 复用开漏 */
    gpio_its.GPIO_Pin   = GPIO_Pin_2;
    gpio_its.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio_its.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &gpio_its);

    gpio_its.GPIO_Pin   = GPIO_Pin_1;
    gpio_its.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio_its.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &gpio_its);

    i2c_its.I2C_ClockSpeed          = HUB_I2C_SPEED_HZ;
    i2c_its.I2C_Mode                = I2C_Mode_I2C;
    i2c_its.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2c_its.I2C_OwnAddress1         = 0x00;
    i2c_its.I2C_Ack                 = I2C_Ack_Enable;
    i2c_its.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(BUS_I2C, &i2c_its);

    I2C_Cmd(BUS_I2C, ENABLE);
}

/* 只发地址看有无 ACK: 1=有设备, 0=无应答 */
uint8_t i2c_bus_probe(uint8_t addr7)
{
    uint32_t t;

    t = I2C_PROBE_LIMIT;
    while (t--) {
        if (I2C_GetFlagStatus(BUS_I2C, I2C_FLAG_BUSY) == RESET) {
            break;
        }
    }
    if (t == 0) {
        return 0;
    }

    I2C_GenerateSTART(BUS_I2C, ENABLE);

    t = I2C_PROBE_LIMIT;
    while (t--) {
        if (I2C_CheckEvent(BUS_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
            break;
        }
    }
    if (t == 0) {
        I2C_GenerateSTOP(BUS_I2C, ENABLE);
        return 0;
    }

    I2C_Send7bitAddress(BUS_I2C, BUS_ADDR8(addr7), I2C_Direction_Transmitter);

    t = I2C_PROBE_LIMIT;
    while (t--) {
        if (I2C_CheckEvent(BUS_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
            I2C_GenerateSTOP(BUS_I2C, ENABLE);
            return 1;               /* 地址被 ACK: 设备在线上 */
        }
    }

    I2C_GenerateSTOP(BUS_I2C, ENABLE);     /* 地址无 ACK: 没接/地址错/上拉或接线问题 */
    return 0;
}

/*==========================================================================
 * 总线诊断(两种模式共用接口)
 *   硬件模式下引脚是 AF_OD, 仍可直接读输入寄存器得到真实线电平;
 *   手动打时钟前先临时切成普通开漏输出, 恢复完再 i2c_bus_init() 还原外设。
 *========================================================================*/
uint8_t i2c_bus_read_lines(void)
{
    uint8_t v = 0;

    if (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_1) == SET) { v |= 0x02; }  /* bit1 = SDA */
    if (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_2) == SET) { v |= 0x01; }  /* bit0 = SCL */
    return v;
}

void i2c_bus_recover(void)
{
    GPIO_InitTypeDef gpio_its = {0};
    uint8_t i;

    /* 临时切成普通开漏输出, 手动驱动两条线 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    gpio_its.GPIO_Pin   = GPIO_Pin_1 | GPIO_Pin_2;
    gpio_its.GPIO_Mode  = GPIO_Mode_Out_OD;
    gpio_its.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &gpio_its);

    GPIO_SetBits(GPIOC, GPIO_Pin_1 | GPIO_Pin_2);
    i2c_bus_delay_us(10);

    for (i = 0; i < 9; i++) {
        if (GPIO_ReadInputDataBit(GPIOC, GPIO_Pin_1) == SET) {
            break;                                   /* SDA 已释放 */
        }
        GPIO_ResetBits(GPIOC, GPIO_Pin_2); i2c_bus_delay_us(5);
        GPIO_SetBits(GPIOC, GPIO_Pin_2);   i2c_bus_delay_us(5);
    }

    /* 补一个规范 STOP: SCL 高时 SDA 由低变高 */
    GPIO_ResetBits(GPIOC, GPIO_Pin_1); i2c_bus_delay_us(5);
    GPIO_SetBits(GPIOC, GPIO_Pin_2);   i2c_bus_delay_us(5);
    GPIO_SetBits(GPIOC, GPIO_Pin_1);   i2c_bus_delay_us(5);

    i2c_bus_init();                                  /* 还原硬件 I2C 外设 */
}

#endif /* HUB_I2C_USE_BITBANG */

/*==========================================================================
 * 公共小件
 *========================================================================*/
uint16_t i2c_bus_get_err_count(void)
{
    return s_bus_err;
}

void i2c_bus_clear_err_count(void)
{
    s_bus_err = 0;
}
