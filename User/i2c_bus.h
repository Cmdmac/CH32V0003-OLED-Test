/********************************** (C) COPYRIGHT *********************************
 * File Name          : i2c_bus.h
 * Description        : CH32V003 I2C1 公共总线层(PC2=SCL, PC1=SDA)
 *
 *   - OLED(SSD1306) 与 INA219 共用这条总线, 底层收发统一在这里
 *   - 两种驱动模式由 hub_config.h 的 HUB_I2C_USE_BITBANG 选择:
 *       =0 硬件 I2C1 外设(WCH EVT 标准库, 事件轮询带超时)
 *       =1 软件 I2C bit-bang(开漏 + 外部上拉, 时序宽松)
 *   - 所有函数返回 1=成功 / 0=失败, 失败不阻塞主流程(计数后直接返回)
 **********************************************************************************/
#ifndef __I2C_BUS_H
#define __I2C_BUS_H

#include <stdint.h>

/* 初始化总线(GPIO/外设, 内部有 once 保护, 重复调用无副作用) */
void i2c_bus_init(void);

/*
 * 写 n 字节: START + ADDR7|W + buf[0..n-1] + STOP
 * @return 1=成功(含从机 ACK), 0=失败(地址无 ACK / 传输超时)
 */
uint8_t i2c_bus_write(uint8_t addr7, const uint8_t *buf, uint16_t n);

/*
 * "写寄存器地址再读数据"复合操作(读取传感器寄存器的标准姿势):
 *   START + ADDR7|W + wbuf[0..wn-1] + (Repeated)START + ADDR7|R + rbuf[0..rn-1] + STOP
 *   最后一字节 NACK。寄存器读: wbuf={reg}, wn=1, rn=按寄存器宽度。
 * @return 1=成功, 0=失败
 */
uint8_t i2c_bus_write_read(uint8_t addr7,
                           const uint8_t *wbuf, uint16_t wn,
                           uint8_t *rbuf, uint16_t rn);

/* 只发地址看有无 ACK(不发数据): 1=设备在线, 0=无应答 */
uint8_t i2c_bus_probe(uint8_t addr7);

/*
 * 读 SCL/SDA 两条线的当前电平(空闲态两根都应为高)。
 * @return bit0 = SCL 电平, bit1 = SDA 电平 (1=高)
 * 用途: SCL 不为高 => 上拉/供电/接线问题; SDA 被拉死在低 => 从机卡住了
 */
uint8_t i2c_bus_read_lines(void);

/*
 * 总线恢复: 从机若在传输中途掉电/复位, 可能把 SDA 一直拉低, 导致后面
 * 所有传输全部失败。这里补发 9 个 SCL 时钟把它"顶"出去, 再补一个 STOP。
 * 调用前应先确认 i2c_bus_read_lines() 的 SDA = 0 才需要恢复。
 */
void i2c_bus_recover(void);

/* 总线级 I2C 传输失败累计(含 OLED 与 INA219 的所有传输) */
uint16_t i2c_bus_get_err_count(void);
void     i2c_bus_clear_err_count(void);

/*
 * 毫秒/微秒阻塞延时(SysTick 方案, 与 WCH EVT debug.c 同构, 独立命名)。
 * 放在总线层是因为 bit-bang 模式的时序翻转离不开微秒延时。
 */
void i2c_bus_delay_us(uint32_t us);
void i2c_bus_delay_ms(uint32_t ms);

#endif /* __I2C_BUS_H */
