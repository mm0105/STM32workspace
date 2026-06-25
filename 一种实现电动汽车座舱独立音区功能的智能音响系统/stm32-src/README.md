# STM32F103C8 串口步进云台示例

本目录是基于 `stm32-exmple/5串口通信.zip` 补齐的 STM32F103C8 Keil 标准外设库工程骨架。当前目录已包含示范工程的 `start/`、`library/`、`public/`、辅助 `user/` 文件，以及本项目的串口步进云台业务源码。

直接打开 `main.uvprojx`，工程分组包括：

- `start`：启动文件、CMSIS 和系统时钟文件。
- `library`：示范工程完整 STM32F10x 标准外设库文件。
- `user`：串口步进业务源码，以及示范工程的 key/led/exti 辅助文件。
- `public`：示范工程公共位带宏和 SysTick 延时文件。

注意：`main.c` 当前运行的是本项目串口步进云台逻辑，不是示范工程原来的串口 LED 示例。

## 协议

- 上位机发送：`MOVE <pan_steps> <tilt_steps>\n`
- 串口链路测试：`PING\n`
- STM32 忙碌：`BUSY\n`
- STM32 解析错误：`ERR\n`
- STM32 完成：`OK\n`
- STM32 链路响应：`PONG\n`

## 引脚

- USART1 TX: PA9
- USART1 RX: PA10
- Pan STEP: PA0
- Pan DIR: PB0
- Tilt STEP: PA1
- Tilt DIR: PB2

默认 USART1 为 `9600 8N1`，与 `stm32-exmple/5串口通信.zip` 原始示范工程的 USART1 设置保持一致。STEP 脉冲由 TIM2 中断调度，两个轴按较大步数同步执行，短轴提前停止。

上电后 `main.c` 只初始化串口和步进输出，不自动执行抬头动作，也不维护坐标。启动抬头、目标丢失归位、限位和跟踪微调都由 Python 按 `yolo-use` 原逻辑计算，再下发相对 `MOVE`。STM32 只是串口到 STEP/DIR 的执行层。
