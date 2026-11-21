# CH32V003 OLED-Test

CH32V003 驱动 SSD1312 / SSD1306 OLED 屏的极简测试工程（128x64）。

从 USB-HUB 项目精简而来，只保留 OLED 屏幕驱动逻辑，用于独立验证屏幕点亮与显示。

## 硬件

- MCU：CH32V003（SOP8，如 CH32V003J4M6）
- 屏幕：0.96 寸 128x64 SSD1312（或 SSD1306）
- 接口：软件 I2C（bit-bang）

### 接线

| CH32V003 | OLED 模组 |
|----------|-----------|
| PA2 (SCL) | SCL（接 2.2k~4.7k 上拉至 3.3V）|
| PA1 (SDA) | SDA（接 2.2k~4.7k 上拉至 3.3V）|
| 3.3V      | VCC |
| GND       | GND |

> 软件 I2C 为开漏输出，必须靠外部上拉电阻拉高，否则屏幕不亮。

## 时钟

系统时钟使用 **48MHz HSI**（内部 24MHz HSI 经 PLL ×2），无需外部晶振。

> 若使用外部晶振版板子，需在 `User/system_ch32v00x.c` 中改回对应的 HSE 配置。

## 测试流程

上电后自动执行：

1. 初始化屏幕（含清屏）
2. 全屏点亮 0.6s（验证 I2C 与屏幕硬件）
3. 显示 4 行测试文字
4. 主循环每 2s 全屏翻转一次（证明程序持续运行）

## 编译

- 工具链：MounRiver Studio（RISC-V Embedded GCC）
- 编译后 `obj/` 目录会同时生成 `.hex` 与 `.bin`
- `.bin` 由 `makefile.init` 自动从 `.elf` 转换，可直接用于 minichlink 烧录：

```
minichlink -C funprog -w obj\OLED-Test.bin 0x08000000
```

## 关键配置

| 配置项 | 位置 | 说明 |
|--------|------|------|
| OLED 型号 | `User/hub_config.h` 的 `HUB_OLED_TYPE` | SSD1312 / SSD1306 切换 |
| I2C 引脚 | `User/i2c_bus.c` | PA2=SCL, PA1=SDA |
| I2C 地址 | `User/hub_config.h` 的 `HUB_I2C_ADDR` | 0x3C（SA0=GND）|
| 时钟源 | `User/system_ch32v00x.c` | 48MHz HSI |

## 许可

见 LICENSE。
