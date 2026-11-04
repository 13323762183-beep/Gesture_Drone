# GestureFlightBridge

ESP32-C3 从 AX7020 UART 接收带 CRC16 和递增序号的 `CMD1` 指令，经 Wi-Fi/MAVLink UDP 控制 XW-Drone。

## 接线

- AX7020 TX → GPIO20（ESP32 RX）
- AX7020 RX ← GPIO21（ESP32 TX，可选）
- GND ↔ GND
- Type-C 用于 ESP32 供电、烧录和 115200 串口调试

## 当前参数

- 起飞：45%
- 保持：30%
- 缓降：25%，5 秒后请求锁机
- 油门变化：15%/秒
- 前后/左右：0.07，变化率 0.05/秒

参数位于 `flight_config.h`。`tune_throttle` 会把起飞和保持油门改成同一个数，仅适合单值联调；不要用它维护当前 45%/30% 的不同目标值。

## USB 串口命令

- `STATUS`：显示会话、Wi-Fi、心跳、解锁和当前动作
- `ENABLE`：满足连接、心跳和未解锁条件时开启手势控制
- `LAND`：进入定时缓降
- `STOP`：紧急请求锁机并关闭会话
- `RELEASE`：停止桥接发送，不代表已完成安全人工接管

注意：AX7020 发来的无手 `STOP` 帧在飞行中按回中/保持油门处理；它与 USB 串口人工输入的 `STOP` 不同。
