/********************************** (C) COPYRIGHT *********************************
 * File Name          : hub_config.h
 * Description        : USB-HUB 应用层配置
 *                      可裁剪项均集中在此, 改完重新编译即可。
 **********************************************************************************/
#ifndef __HUB_CONFIG_H
#define __HUB_CONFIG_H

/*====================================================================
 * SSD1306 I2C 参数
 * CH32V003 硬件 I2C1: PC2 = SCL, PC1 = SDA (复用开漏, 需外部上拉电阻)
 *===================================================================*/
#define HUB_I2C_ADDR        0x3C     /* SSD1306 7bit 地址: SA0=GND->0x3C, SA0=VCC->0x3D */
                                     /* (驱动内部 I2C_Send7bitAddress 需左移 1 位,
                                        故这里按习惯写 7 位值, 勿填 0x78) */
#define HUB_I2C_SPEED_HZ    100000   /* 硬件I2C模式专用(软件bit-bang不用此值):
                                      * 调试期先用 100kHz(时序最稳), 点亮且稳定后再试
                                      * 400000 提速; 400k 需较强上拉(2.2k) */

/* I2C 驱动模式:
 *   1 = 软件 I2C(bit-bang, PC2=SCL/PC1=SDA, 时序宽松)—— 当前调试交叉验证用
 *   0 = 硬件 I2C1 外设(WCH EVT 标准库)
 * 结论: 软件模式能点亮 => 硬件I2C外设配置问题; 软件模式也黑 => 电气/接线/面板问题 */
#define HUB_I2C_USE_BITBANG 1

/*--------------------------------------------------------------------
 * bit-bang 时序余量(只对 HUB_I2C_USE_BITBANG=1 有效)
 *   HUB_I2C_BITBANG_HALF_US: 半位周期(us)
 *       5  -> 约 100kHz(默认)
 *       8~10 -> 降速到 50~60kHz, 给"上拉偏弱/杜邦线偏长导致沿变缓"
 *               留余量 —— 偶发错字(表现为屏幕半屏错乱、err 计数上涨)时先试这个
 *   HUB_I2C_SCL_WAIT_US: 拉高 SCL 后最多等多久(us)确认线上电平真的到高
 *       0  -> 不等(原来的固定延时行为)
 *       20 -> 等, 补偿缓慢上升沿 + 兼容从机时钟拉伸(推荐)
 *------------------------------------------------------------------*/
#define HUB_I2C_BITBANG_HALF_US  5
#define HUB_I2C_SCL_WAIT_US      20

/*====================================================================
 * OLED 屏型号选择(三选一, 改 HUB_OLED_TYPE 即可)
 *   HUB_OLED_0P91_SSD1306 : 0.91 寸 128x32 SSD1306   <- 默认(当前在用)
 *   HUB_OLED_0P96_SSD1312 : 0.96 寸 128x64 SSD1312   <- 本次新增
 *   HUB_OLED_0P96_SSD1306 : 0.96 寸 128x64 SSD1306   (备选, 模块实为 1306 时选它)
 *
 * !!! SSD1312 与 SSD1306 命令集大体兼容, 但以下参数取值不同, 选错会
 *     黑屏/镜像/过暗:  电荷泵 8Dh、SEG 配置 DAh、预充电 D9h、
 *     VCOMH DBh、段重映射 A0h/A1h、对比度 81h,
 *     以及 SSD1312 独有的 ADh(IREF 选择, 见下方 HUB_SSD1306_IREF)。
 *     分辨率由 HUB_OLED_H 自动决定, 布局在 hub.c 里按 32/64 自动切换。
 *
 * 贴装方向微调(眼睛看着调, 改完重新编译):
 *   文字左右镜像 -> 把 HUB_SSD1306_SEG 在 0xA0/0xA1 之间对调
 *   文字上下颠倒 -> 把 HUB_SSD1306_COM 在 0xC8/0xC0 之间对调
 *===================================================================*/
#define HUB_OLED_0P91_SSD1306   1
#define HUB_OLED_0P96_SSD1312   2
#define HUB_OLED_0P96_SSD1306   3

/* !!! 当前值 = 临时诊断用(配合 HUB_OLED_TEST_MODE=1 去判 0.96 那块屏):
 *     诊断完请把下行改回 HUB_OLED_0P91_SSD1306(现在在用的好屏)。 */
#define HUB_OLED_TYPE           HUB_OLED_0P96_SSD1312

/* ADh = External or internal IREF Selection (SSD1312 独有, SSD1306 没有这条)
 *   SSD1312 命令表(p40): 数据字节 = 0 1 0 A[4] 0 0 0 0
 *     A[4]=0 -> Select external IREF  <- 复位默认, 需要 IREF 脚外挂电阻对 VSS
 *     A[4]=1 -> Enable internal IREF during display ON
 *   0.96 寸模组普遍不装 IREF 电阻, 复位后走"外部 IREF" => 段驱动器没有
 *   参考电流 => 面板一个点都不亮, 但 I2C 侧完全正常(err=0)。所以必须发 0x50。
 *   0x00 = 不发这条命令(SSD1306 没有 ADh, 发了会被当未知命令) */
#define HUB_SSD1306_IREF_INT    0x50   /* ADh: 用内部 IREF(无外挂电阻时必须) */
#define HUB_SSD1306_IREF_EXT    0x40   /* ADh: 显式选外部 IREF(有外挂电阻时) */

#if   (HUB_OLED_TYPE == HUB_OLED_0P96_SSD1312)
  /* ---- 0.96 寸 128x64 SSD1312 ---- */
  #define HUB_OLED_H            64
  #define HUB_SSD1306_SEG       0xA1   /* 段重映射(左右方向): 实测本模组用 0xA0 会左右镜像
                                        * (文字顺序和单个字都反), 故改 0xA1。
                                        * 0xA0 <-> 0xA1 只影响左右, 不影响能否点亮 */
  #define HUB_SSD1306_COM       0xC0   /* COM 扫描: 实测本模组上下颠倒, 故用 0xC0
                                        * (0xC8 <-> 0xC0 对调即可, 只影响上下方向,
                                        *  不影响能否点亮; 左右镜像改 SEG) */
  #define HUB_SSD1306_COMCFG    0x10   /* DAh SEG 引脚配置: SSD1312 复位默认 0x10 */
  #define HUB_SSD1306_CP        0x72   /* 8Dh 电荷泵: 12h=7.5V 52h=8V 72h=9V 92h=10V
                                        * (SSD1312 只认 12h/52h/72h/92h, 0x14 无效!) */
  #define HUB_SSD1306_PRECHG    0x22   /* D9h 预充电周期 */
  #define HUB_SSD1306_VCOMH     0x30   /* DBh VCOMH 电平 */
  #define HUB_SSD1306_CONTRAST  0x7F   /* 81h 对比度(01~FF) */
  #define HUB_SSD1306_IREF      HUB_SSD1306_IREF_INT
#elif (HUB_OLED_TYPE == HUB_OLED_0P96_SSD1306)
  /* ---- 0.96 寸 128x64 SSD1306 ---- */
  #define HUB_OLED_H            64
  #define HUB_SSD1306_SEG       0xA1
  #define HUB_SSD1306_COM       0xC0   /* 同一块 0.96" 模组, 贴装方向一致 -> 同 0xC0 */
  #define HUB_SSD1306_COMCFG    0x12   /* DAh COM 引脚配置: 128x64 用 0x12 */
  #define HUB_SSD1306_CP        0x14
  #define HUB_SSD1306_PRECHG    0xF1
  #define HUB_SSD1306_VCOMH     0x40
  #define HUB_SSD1306_CONTRAST  0xCF
  #define HUB_SSD1306_IREF      0x00   /* SSD1306 无 ADh 命令 */
#else
  /* ---- 0.91 寸 128x32 SSD1306 ---- */
  #define HUB_OLED_H            32
  #define HUB_SSD1306_SEG       0xA1
  #define HUB_SSD1306_COM       0xC8
  #define HUB_SSD1306_COMCFG    0x02   /* DAh COM 引脚配置: 128x32 用 0x02 */
  #define HUB_SSD1306_CP        0x14
  #define HUB_SSD1306_PRECHG    0xF1
  #define HUB_SSD1306_VCOMH     0x40
  #define HUB_SSD1306_CONTRAST  0xFF
  #define HUB_SSD1306_IREF      0x00   /* SSD1306 无 ADh 命令 */
#endif

/*====================================================================
 * 显示格式
 *   OUT1/OUT2 每行固定 16 字符(8x16 字模 x16 = 128 像素满宽)
 *   电流单位规则: >=1000mA 显示为 A(两位小数), <1000mA 显示为整数 mA
 *===================================================================*/
#define HUB_CURRENT_A_TH_MA 1000     /* 电流切换单位阈值: >=此值显示 A */

/*====================================================================
 * 抗"半屏错乱"两项设置
 *--------------------------------------------------------------------
 * HUB_OLED_FLUSH_RETRY: 整帧上屏失败(某字节从机没 ACK)后的重试次数。
 *   失败的危害: 从机 DDRAM 少收若干字节 -> 整帧内容前移 -> 屏幕上就是
 *   "上半屏正常、下半屏错位/错乱"。重试时会重新设一遍寻址窗口(21h/22h),
 *   把从机指针拉回 (0,0) 再整帧重发, 是这类坏帧唯一的可靠修法。
 *   0 = 不重试(不推荐)。
 *
 * HUB_FORCE_REFRESH_EVERY: 数值长时间不变时, 每隔这么多次主循环强制
 *   重发一帧(0 = 不强制)。作用: 万一从机收错了字节但 ACK 正常(纯电气
 *   干扰, 错误计数查不出来), 坏屏不会一直挂着 —— 最多 2 秒就被下一帧
 *   覆盖掉。按 HUB_POLL_MS=100 算, 20 次约 2 秒。
 *===================================================================*/
#define HUB_OLED_FLUSH_RETRY       3
#define HUB_FORCE_REFRESH_EVERY    20

/*====================================================================
 * 屏幕点亮测试模式(排查"屏幕不亮"专用)  <- 当前 = 1, 诊断完请置回 0
 *   1 = 上电后不进主循环, 改跑 oled_test_run(): 扫总线 + 依次试 9 组
 *       候选参数(控制器族 x 升压 x IREF), 每组先来一下 0xA5 全屏白闪,
 *       再画信息帧。看到哪一组白闪/显示出来, 就记下编号, 回到上面的
 *       型号预设对上即可。
 *   0 = 正常运行
 *
 * 配合要求(否则结果无法解释/编译就直接报错):
 *   - HUB_OLED_TYPE 必须是 0.96 预设(候选表全是 64 行, 见 oled_test.c
 *     的 #error), 屏也要换成那块 0.96 的;
 *   - OLED_TEST_WHITE_STEPS 必须是 1(0xA5 全屏白是"面板活不活"的判据);
 *   - 日志由下面的 HUB_OLED_TEST_LOG 控制: 默认 0 = 不 printf =>
 *     **不需要插 WCH-LinkE**, 任何接法都不会卡死。
 *===================================================================*/
#define HUB_OLED_TEST_MODE  0

/*====================================================================
 * 测试模式里的 SDI 日志开关  <- 当前 = 0
 *   0 = 把 oled_test.c 里的 printf 整体编译掉(完全不碰调试邮箱):
 *       跑完 9 组**不需要调试器**, 进度靠 LED 连闪组号(n 下 = 第 n 组)
 *       + 屏上信息帧的 "CFG n/9"。这是推荐值。
 *   1 = 打印 "-- addr ... cfg n/9" / "err=.. flush=.." 等日志。开启前提
 *       (三项缺一不可, 否则必然卡死):
 *         ① LinkE 在线且没被别的会话占用;
 *         ② 下载时勾了 Enable SDI Printf, 且**勾完重新下载过**
 *            (MRS: Properties -> Download -> Download Settings;
 *             或 WCH-LinkUtility 的 Target 菜单);
 *         ③ 串口助手已打开 LinkE 的虚拟 COM 口(115200 8-N-1)。
 *       少任何一项, debug.c:185 的 `while(*0xE00000F4)` 就**无超时死等**
 *       => 卡在日志那一句, 后面的 0xA5 白闪全部执行不到, 一轮测试白跑。
 *===================================================================*/
#define HUB_OLED_TEST_LOG   1

/*====================================================================
 * INA219 电压/电流采样(OUT1/OUT2 各一片, 与 SSD1306 共享 I2C1 总线)
 * 7 位地址(用户指定):
 *   OUT1 -> 1000000 = 0x40  (A1=GND, A0=GND)
 *   OUT2 -> 1000001 = 0x41  (A1=GND, A0=VCC/3.3V)
 * 量程: 16V / PGA +-320mV, 采样电阻默认 0.1R -> 电流满量程 +-3.27A
 
 * !!! 换采样电阻只改 HUB_INA219_SHUNT_MOHM, 并确认
 *     满量程电流 * R_SHUNT <= 320mV, 否则调小 ina219.c 的 PGA
 *===================================================================*/
#define HUB_USE_INA219          0     /* 1 = 读真实 INA219; 0 = 演示三角波数据 */
#define HUB_INA219_ADDR_CH1     0x40  /* OUT1 的 INA219 (二进制 1000000) */
#define HUB_INA219_ADDR_CH2     0x41  /* OUT2 的 INA219 (二进制 1000001) */
#define HUB_INA219_SHUNT_MOHM   100   /* 采样电阻, 单位 mΩ —— 按实际板子修改! */

/*====================================================================
 * 主循环刷新周期(ms)与测量步长
 *   main 中每 HUB_POLL_MS 调用一次 measure_poll() + hub_refresh()
 *   修改此值会同时影响演示数据变化速度, 请与 main 保持一致
 *===================================================================*/
#define HUB_POLL_MS         100

/*====================================================================
 * 开机自检打印开关
 *   1 = 上电跑 usb_hub_diag()(SDI 打印 + 全屏点亮 1s), 调试期用
 *   0 = 跳过(独立运行/量产固件)
 * 注意: SDI 打印需要 WCH-LinkE 在线读取, 脱离调试器单独上电时
 *       printf 会卡在等调试器读邮箱 —— 所以独立运行前必须置 0。
 *===================================================================*/
#define HUB_ENABLE_DIAG     0

#endif /* __HUB_CONFIG_H */
