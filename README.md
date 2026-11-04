# Gesture Drone：AX7020 板端手势控制无人机

本项目使用 OV5640、ALINX AX7020（Zynq-7020）、WeAct ESP32-C3 与 XW-Drone，完成：

```text
OV5640 图像 → AX7020 PS 端 TinyCNN 识别 → UART CMD1
            → ESP32-C3 → Wi-Fi / MAVLink UDP → 无人机飞控
```

PL 负责摄像头/VDMA/HDMI 等图像通路，TinyCNN 当前运行在 Zynq PS（ARM）端；本仓库不声称已经实现 PL 神经网络加速。

## 当前动作

| 画面/操作 | AX7020 输出 | 无人机行为 |
| --- | --- | --- |
| V | `UNLOCK` | 请求解锁，油门渐变至 45% |
| 张掌居中 | `HOLD` | 回中，目标油门 30% |
| 张掌位于上下左右 | `FORWARD/BACK/LEFT/RIGHT` | 在 30% 油门基础上给小幅俯仰/横滚 |
| 握拳 | `LAND` | 锁存 25% 油门缓降，5 秒后请求锁机 |
| 无手 | `STOP` 帧 | **回中并保持 30% 油门，不会停转或锁机** |
| USB 串口输入 `STOP` | — | 紧急请求锁机并关闭会话 |

AX7020 命令中断超过 350 ms 或无人机心跳失效时，桥接器会请求锁机并进入故障状态。无线链路无法保证命令一定送达，因此软件急停不能替代物理断电。

## 仓库结构

```text
AX7020/                 可直接下载运行的 bit、elf、TCL 和串口工具
AX7020_SDK/             Xilinx SDK 2019.1 工程：硬件平台、BSP、应用源码
training/               数据预处理、TinyCNN 训练、导出与自检工具
WeAct/GestureFlightBridge/
                        ESP32-C3 Wi-Fi/MAVLink 桥接源码
docs/                   文件审计说明
```

原始训练图片、视频、Vivado/SDK 缓存和编译中间文件未纳入仓库。最终训练权重和模型位于 `training/final_model/`，部署中的同一份权重位于 `AX7020_SDK/tinycnn_app/src/tiny_gesture_weights.h`。

## 直接运行

### 1. AX7020

1. 连接 OV5640、HDMI、JTAG、UART 并给 AX7020 上电。
2. 让识别区域保持空白静止。
3. 双击 `AX7020/run_uart_test.bat`。
4. 等待 `PROGRAM_RUN_OK`；校准完成后再把手放入画面。

脚本默认使用 `D:\Xilinx\SDK\2019.1\bin\xsct.bat`。AX7020 串口为 115200，可运行 `AX7020/monitor.bat` 查看输出。

### 2. ESP32-C3

接线：

- AX7020 UART TX → ESP32-C3 GPIO20（RX）
- AX7020 UART RX ← ESP32-C3 GPIO21（TX，可选）
- GND ↔ GND
- ESP32-C3 Type-C 接电脑

在 Arduino/Mixly 中打开 `WeAct/GestureFlightBridge/GestureFlightBridge.ino`，选择 ESP32-C3，开启 USB CDC On Boot 后上传。上传前关闭所有占用 ESP32 串口的软件。

当前 `flight_config.h` 参数：起飞 45%、保持 30%、下降 25%、油门变化 15%/秒、下降 5 秒后锁机、方向量 0.07。这些是当前样机演示值，不是通用安全参数。

### 3. 联机

1. 无人机最后上电，先拆桨或在安全空旷区域固定测试。
2. 打开 ESP32 USB 串口（115200），发送 `STATUS`。
3. 确认 `state=GROUNDED wifi=1 hb=1 armed=0`。
4. 发送 `ENABLE`，先移开 V，再重新做 V。
5. 演示结束用握拳缓降；紧急情况在串口发送 `STOP` 或物理断电。

缓降结束或急停后，再次启动必须重新发送 `ENABLE`。

## 重新编译 AX7020 应用

使用 Xilinx SDK 2019.1，新建空 workspace 后选择 `File → Import → General → Existing Projects into Workspace`。根目录选 `AX7020_SDK`，导入 `hw_platform`、`tinycnn_app_bsp`、`tinycnn_app`，依次构建 BSP 和应用。

## 重新训练

见 [`training/README.md`](training/README.md)。训练依赖写在 `requirements-training.txt`，仓库不包含原始数据集。最终模型为 V / OPEN / FIST 三分类；无手由前景面积阈值判断，不作为 CNN 类别。

## 安全与局限

- 识别受背景、光照、手势大小和摄像头色偏影响。
- “保持油门”不是闭环定高；实际悬停仍依赖飞控与样机状态。
- 无手不会停电机；正常结束用握拳，紧急停机用 USB `STOP` 或物理断电。
- 所有参数都应先拆桨验证，再进行低空、可随时断电的演示。

## 授权

新增原创说明文件采用 MIT License。既有工程、固件及第三方代码的权利边界见 `THIRD_PARTY_NOTICES.md`。
