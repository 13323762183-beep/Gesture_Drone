# Gesture Drone：基于 AX7020 的嵌入式手势无人机控制

本项目面向**嵌入式 FPGA 大赛 AMD 赛道·自主命题初级赛道**，探索将视觉手势识别与无人机控制相结合的人机交互方式。使用 ALINX AX7020（AMD/Xilinx Zynq-7000 XC7Z020）开发板，配合 OV5640 摄像头、WeAct ESP32-C3 和 XW-Drone，构建从图像输入到无线控制的嵌入式链路。

项目可用于嵌入式视觉教学、手势交互实验及无人机地面控制原型。摄像头与识别部署在板端，ESP32-C3 负责协议桥接，电脑用于下载和调试。选择 AMD Zynq 平台，结合可编程逻辑配置、ARM 裸机应用和外设通信，符合本项目对 AMD 赛道自主命题的技术定位。**具体参赛资格及评分以主办方当届规则为准；本仓库不声称已完成全部赛道考核。**

> 当前为拆桨电机联调运行包，不是经验证的自主飞行系统。配置已开启电机输出（`FLIGHT_OUTPUT_ENABLED=true`），仅在拆除螺旋桨后联调。握拳触发立即请求锁定，不能视为空中平稳降落。

## 系统链路

```text
OV5640 → AX7020 板端 TinyCNN → UART CMD1
       → WeAct ESP32-C3 → Wi-Fi / MAVLink UDP → XW-Drone
```

当前交付包含 AX7020 预编译 bitstream、ARM ELF 和 ESP32-C3 源码。未包含 Vivado 工程、AX7020 应用源码、训练数据及模型训练代码，因此支持部署现有固件，尚不能从本仓库独立重建整套系统。不能仅凭运行包断言 TinyCNN 在 PL 中实现硬件加速。

## 已实现功能

| 输入或操作 | 当前代码行为 |
| --- | --- |
| V / UNLOCK | 会话允许、心跳有效且飞控未解锁时请求解锁；再次触发前需先松开 V |
| 张掌 / HOLD | 输出居中控制与配置的保持油门；不代表闭环定高 |
| 张掌方向 / FORWARD、BACK、LEFT、RIGHT | 输出小幅俯仰或横滚控制 |
| 握拳 / LAND 帧 | 立即请求锁定，禁用会话，并每 50ms 重发锁定请求 |
| 无手 / STOP 帧 | 控制居中，使用保持油门；与 USB STOP 不同 |
| USB ENABLE | 满足连接和未解锁条件后开启控制会话 |
| USB STOP | 请求锁定并禁用会话 |
| USB LAND | 进入配置下降油门输出；当前不提供定时自动落地保证 |
| USB RELEASE | 停止桥接控制发送；不保证人工接管已完成 |
| USB STATUS | 显示连接、心跳、解锁、会话和配置状态 |

桥接器检查 CMD1 的 CRC16、字段范围、手势一致性和递增序号。AX7020 帧有效期为 350ms，飞控心跳有效期为 1500ms；已解锁状态下超时会请求锁定。无线断开时命令可能无法送达，实际电机行为还依赖飞控失联保护。

## 软硬件环境

| 类别 | 要求 |
| --- | --- |
| 视觉主控 | ALINX AX7020（Zynq XC7Z020），与现有固件匹配的 OV5640 摄像头及供电 |
| 通信桥接 | WeAct ESP32-C3、USB Type-C 数据线 |
| 无人机 | XW-Drone，支持当前 MAVLink 指令及 UDP 14550 |
| 下载调试 | AX7020 JTAG 下载器、适配板卡的 USB 串口、共地连接线 |
| 主机 | Windows、PowerShell；运行包脚本面向 Windows |
| AX7020 工具 | Xilinx SDK 2019.1 / XSCT，JTAG 驱动及硬件服务 localhost:3121 |
| ESP32 工具 | Arduino CLI、Arduino ESP32 开发板包；FQBN `esp32:esp32:esp32c3:CDCOnBoot=cdc` |
| 可选串口监视 | Python 3.10+、pyserial：`python -m pip install -r requirements.txt` |

ESP32 开发板包版本未随原包固定，需在本机验证兼容性。Vivado 用于重建硬件设计，但当前运行包未包含可供重建的工程。

## 接线

- AX7020 UART0 TX → ESP32-C3 GPIO20（RX）。
- AX7020 UART0 RX ← ESP32-C3 GPIO21（TX，可选）。
- 两板 GND 共地，确认使用兼容的 3.3V UART 电平。
- ESP32-C3 使用 Type-C 供电；AX7020 与无人机使用各自匹配电源。

## 运行步骤

1. **拆除无人机螺旋桨**，固定机身，准备物理断电方式。关闭其他控制端及占用串口的软件。
2. 检查 `WeAct/GestureFlightBridge/flight_config.h` 中 Wi-Fi、目标 IP、串口引脚和油门参数。默认热点为 `XW_Drone_WiFi`，密码为示例 `12345678`，目标为 `192.168.4.1:14550`，按实际设备修改。
3. 检查 `AX7020/run_uart_test.bat` 的 XSCT 路径，默认 `D:\Xilinx\SDK\2019.1\bin\xsct.bat`。连接 JTAG，启动对应硬件服务，为 AX7020 上电。保持摄像头识别区域空白静止，运行该脚本。
4. 等待 `PROGRAM_RUN_OK` 和 `CALIBRATION_READY`。若无法找到 APU，关闭板卡电源约 5 秒后重试；若校准未完成，检查画面及背景。
5. 检查 `WeAct/tune_throttle.ps1` 中 Arduino CLI 和配置文件路径，默认位于 `D:\mixly\arduino-cli`。使用以下命令编译并上传（端口按实际修改）：

```powershell
powershell -ExecutionPolicy Bypass -File .\WeAct\tune_throttle.ps1 -Percent 50 -Port COM12
```

该命令同时修改解锁后和保持状态的目标油门，再编译烧录；50% 是原包联调值，不是通用起飞参数。也可在 Arduino IDE 中打开 sketch，选择 ESP32-C3 并开启 USB CDC On Boot 后上传。

6. 打开 ESP32 USB 串口，波特率 115200，发送 `STATUS`。确认 `state=GROUNDED wifi=1 hb=1 armed=0` 及配置就绪。
7. 发送 `ENABLE`，先松开 V 再摆 V，观察解锁和动作日志。张掌测试方向，握拳测试锁定。再次测试需要重新发送 `ENABLE`。
8. 结束时发送 `STOP` 并确认飞控未解锁，随后断电。软件停机请求不能替代物理断电。

AX7020 串口为 115200，可使用 `AX7020/monitor.bat` 或：

```powershell
python .\AX7020\uart_gesture_monitor.py --list
python .\AX7020\uart_gesture_monitor.py COM7 --seconds 60 --csv run.csv
```

监视器支持调试输出和离线回放；避免与桥接器同时占用同一串口。

## 当前配置

解锁后目标油门与保持油门均为 50%，USB LAND 油门为 24%；油门变化率为每秒 12 个百分点，方向幅度为 0.07。`FLIGHT_TAKEOFF_MS` 和 `FLIGHT_LAND_MS` 均为 0；当前代码不能据此宣称自动定时起降。上述值仅记录交付配置，并不证明适合实际飞行。

## 项目结构

```text
Gesture_Drone/
├── README.md
├── LICENSE                         # 本次新增原创说明文档的 MIT 授权
├── THIRD_PARTY_NOTICES.md           # 既有文件授权边界
├── requirements.txt                # 可选串口工具依赖
├── .gitignore
├── docs/
│   └── FILE_AUDIT.md                # 整理及验证记录
├── AX7020/
│   ├── design_1_wrapper.bit         # FPGA 配置文件，必须保留
│   ├── tinycnn_app.elf              # ARM 应用，必须保留
│   ├── ps7_init.tcl                # PS 初始化
│   ├── run_uart_test.bat
│   ├── run_uart_test.tcl            # JTAG 下载与校准检查
│   ├── monitor.bat
│   └── uart_gesture_monitor.py
└── WeAct/
    ├── tune_throttle.bat
    ├── tune_throttle.ps1
    └── GestureFlightBridge/
        ├── GestureFlightBridge.ino  # MAVLink 桥接与状态机
        ├── command_protocol.h      # CMD1 解析与自检
        ├── flight_config.h
        └── README.md
```

## 验证状态与局限

本次整理检查了文件完整性、Python 语法和文档与桥接源码的一致性；未连接硬件，未执行 JTAG 下载、ESP32 编译烧录或飞行验证。没有测得的识别准确率、端到端延迟或续航指标，故不提供此类性能承诺。实际识别受光照、背景和手势姿态影响。

## 授权

本次新增原创说明文档采用 MIT License。原包未附完整授权证明，原有源码、预编译固件及其第三方组成部分保留原权利人的权利，参见 `THIRD_PARTY_NOTICES.md`；公开发布不自动赋予这些部分 MIT 授权。
