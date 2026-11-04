/************************************************************************/
/*  gesture_link.h  --  AX7020 -> ESP32-C3 手势指令链路                    */
/*                                                                       */
/*  协议（ASCII 行，115200 8N1，UART0 / MIO14 MIO15）：                    */
/*      板端 -> 模块：  "G:<n>\r\n"   n = 0 NONE / 1 V / 2 OPEN / 3 FIST  */
/*                      "HB\r\n"      1 Hz 心跳，用于模块判断板子是否活着   */
/*      模块 -> 板端：  "ACK:...\r\n" 仅调试用，板端可以完全不读           */
/*                                                                       */
/*  约束：                                                                */
/*    * G:n 只在手势**变化**时发送，重复手势不会重复触发                   */
/*    * 模块收到任意整行都算保活；10 秒没有整行就自动停止向无人机发包       */
/************************************************************************/
#ifndef GESTURE_LINK_H
#define GESTURE_LINK_H

#include "xil_types.h"

#define GESTURE_NONE 0u
#define GESTURE_V    1u
#define GESTURE_OPEN 2u
#define GESTURE_FIST 3u

/* 自检：上电后循环发送 G:0..3，仅用于验证链路。
 * 识别算法接入后必须改成 0，否则会向无人机发送假手势。 */
#define GESTURE_LINK_SELFTEST 0

/* 绑定发送函数（传 display_demo.c 里的 result_uart_send） */
void GestureLink_Init(void (*send_fn)(const char *msg));

/* 上报手势；与上次相同则忽略。g 超出 0..3 时按 NONE 处理。 */
void GestureLink_Send(u8 g);

/* 主循环调用：负责 1 Hz 心跳和可选的 selftest。调用周期需固定为 10 ms。 */
void GestureLink_Poll(void);

#endif /* GESTURE_LINK_H */
