/************************************************************************/
/*																		*/
/*	display_demo.c	--	ALINX AX7010 HDMI Display demonstration 						*/
/*																		*/
/************************************************************************/

/* ------------------------------------------------------------ */
/*				Include File Definitions						*/
/* ------------------------------------------------------------ */

#include "display_demo.h"
#include "display_ctrl/display_ctrl.h"
#include <stdio.h>
#include "math.h"
#include <ctype.h>
#include <stdlib.h>
#include "xil_types.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xiicps.h"
#include "vdma.h"
#include "i2c/PS_i2c.h"
#include "xgpio.h"
#include "sleep.h"
#include "ov5640.h"
#include "xuartps.h"
#include "gesture_link.h"
#include "tiny_gesture_pipeline.h"
#include "xtime_l.h"
/*
 * XPAR redefines
 */
#define DYNCLK_BASEADDR XPAR_AXI_DYNCLK_0_BASEADDR
#define VGA_VDMA_ID XPAR_AXIVDMA_0_DEVICE_ID
#define DISP_VTC_ID XPAR_VTC_0_DEVICE_ID
#define VID_VTC_IRPT_ID XPS_FPGA3_INT_ID
#define VID_GPIO_IRPT_ID XPS_FPGA4_INT_ID
#define SCU_TIMER_ID XPAR_SCUTIMER_DEVICE_ID
#define UART_BASEADDR XPAR_PS7_UART_1_BASEADDR


/* ------------------------------------------------------------ */
/*				Global Variables								*/
/* ------------------------------------------------------------ */

/*
 * Display Driver structs
 */
DisplayCtrl dispCtrl;
XAxiVdma vdma;
XIicPs ps_i2c0;
XGpio cmos_rstn;
XUartPs result_uart;
/*
 * Framebuffers for video data
 */
u8 frameBuf[DISPLAY_NUM_FRAMES][DEMO_MAX_FRAME] __attribute__ ((aligned(64)));
u8 *pFrames[DISPLAY_NUM_FRAMES]; //array of pointers to the frame buffers

#define DIAG_BASE_ADDR 0x02000000U
static volatile u32 * const diag = (volatile u32 *)DIAG_BASE_ADDR;

static void diag_write(u32 stage, u32 value)
{
	diag[0] = 0x4F563536U; /* "OV56" */
	diag[1] = stage;
	diag[2] = value;
	Xil_DCacheFlushRange(DIAG_BASE_ADDR, 12U);
}

static int result_uart_init(void)
{
	XUartPs_Config *config = XUartPs_LookupConfig(XPAR_XUARTPS_0_DEVICE_ID);
	int status;

	if (config == NULL) return XST_FAILURE;
	status = XUartPs_CfgInitialize(&result_uart, config, config->BaseAddress);
	if (status != XST_SUCCESS) return status;
	return XUartPs_SetBaudRate(&result_uart, 115200);
}

static void result_uart_send(const char *message)
{
 while(*message)XUartPs_SendByte(XPAR_PS7_UART_0_BASEADDR,(u8)*message++);
 while(!(XUartPs_ReadReg(XPAR_PS7_UART_0_BASEADDR,XUARTPS_SR_OFFSET)&XUARTPS_SR_TXEMPTY)){}
}
#include "control_link.h"

/* ------------------------------------------------------------ */
/*         手势指令链路（AX7020 -> ESP32-C3）                     */
/* ------------------------------------------------------------ */
/* 协议：G:<n>\r\n  n = 0 NONE / 1 V / 2 OPEN / 3 FIST            */
/*       HB\r\n     1 Hz 心跳                                     */
/* 为什么写在这里而不是单独的 .c：SDK 工程的 makefile 是显式列      */
/* 源文件的，新增 .c 不会自动参与编译。整段只有 60 行，放这里最省事。*/

#define POLL_PERIOD_MS   10u
#define HB_TICKS         (1000u / POLL_PERIOD_MS)

static void (*gl_send)(const char *msg) = 0;
static u8  gl_last = 0xFFu;   /* 0xFF = 从未发送 */
static u32 gl_ticks = 0u;

void GestureLink_Send(u8 g);

void GestureLink_Init(void (*send_fn)(const char *msg))
{
	gl_send = send_fn;
	gl_last = 0xFFu;
	gl_ticks = 0u;
	GestureLink_Send(GESTURE_NONE);   /* 上电先报一次，确认方向没接反 */
}

void GestureLink_Send(u8 g)
{
	char buf[8];

	if (!gl_send) {
		return;                        /* UART0 没初始化成功，不能空指针调用 */
	}
	if (g > GESTURE_FIST) {
		g = GESTURE_NONE;
	}
	if (g == gl_last) {
		return;                        /* 去重 */
	}
	gl_last = g;

	buf[0] = 'G';
	buf[1] = ':';
	buf[2] = (char)('0' + g);
	buf[3] = '\r';
	buf[4] = '\n';
	buf[5] = '\0';
	gl_send(buf);
}

void GestureLink_Poll(void)
{
	if (!gl_send) {
		return;
	}
	gl_ticks++;
	if (gl_ticks >= HB_TICKS) {
		gl_ticks = 0u;
		gl_send("HB\r\n");
	}
}

/* ------------------------------------------------------------ */
/*				Procedure Definitions							*/
/* ------------------------------------------------------------ */



int main(void)
{

	int Status;
	int ResultUartReady;
	XAxiVdma_Config *vdmaConfig;
	int i;
	u8 chip_id_high;
	u8 chip_id_low;

	memset((void *)DIAG_BASE_ADDR, 0, 56U);
	Xil_DCacheFlushRange(DIAG_BASE_ADDR, 56U);
	diag_write(1U, 0U);
	/*
	 * Initialize an array of pointers to the 3 frame buffers
	 */
	/* Camera capture and HDMI scanout must use the same physical frame.
	 * The display VDMA is configured for circular mode; giving it three
	 * different buffers while the camera updates only one makes two of every
	 * three displayed frames stale/black.  Alias every display frame store to
	 * the live camera buffer so both VDMAs always address identical data. */
	memset(frameBuf[0], 0, DEMO_MAX_FRAME);
	Xil_DCacheFlushRange((INTPTR) frameBuf[0], DEMO_MAX_FRAME);
	for (i = 0; i < DISPLAY_NUM_FRAMES; i++)
	{
		pFrames[i] = frameBuf[0];
	}

	xil_printf("AX7020 OV5640 HDMI 720p start\r\n");
	if (!gesture_command_selftest()) {
		xil_printf("ERROR: GESTURE_COMMAND_SELFTEST failed\r\n");
		return XST_FAILURE;
	}
	xil_printf("GESTURE_COMMAND_SELFTEST PASS\r\n");
	ResultUartReady = (result_uart_init() == XST_SUCCESS);
	xil_printf("Result UART0: %s\r\n", ResultUartReady ? "ready" : "FAILED");
	if (ResultUartReady) {
		gc_key_init(); result_uart_send("GC_HELLO\r\n");
		/* 绑定手势上报链路：上电先发一次 G:0，确认方向没接反 */
		GestureLink_Init(result_uart_send);
	}
	i2c_init(&ps_i2c0, XPAR_XIICPS_0_DEVICE_ID,100000);
	diag_write(2U, 0U);

	XGpio_Initialize(&cmos_rstn, XPAR_CMOS_RST_DEVICE_ID);   //initialize GPIO IP
	XGpio_SetDataDirection(&cmos_rstn, 1, 0x0);            //set GPIO as output
	/* bit 0: RESET (active low), bit 1: PWDN (active high) */
	XGpio_DiscreteWrite(&cmos_rstn, 1, 0x2);
	usleep(500000);
	XGpio_DiscreteWrite(&cmos_rstn, 1, 0x0);               //set GPIO output value to 0

	usleep(500000);
	XGpio_DiscreteWrite(&cmos_rstn, 1, 0x1);
	usleep(500000);

	Status = ov5640_read(&ps_i2c0, 0x300A, &chip_id_high);
	if (Status == XST_SUCCESS) Status = ov5640_read(&ps_i2c0, 0x300B, &chip_id_low);
	if (Status != XST_SUCCESS) {
		diag_write(0xE1U, (u32)Status);
		xil_printf("ERROR: OV5640 I2C read failed: %d\r\n", Status);
		return XST_FAILURE;
	}
	xil_printf("OV5640 chip ID: 0x%02x%02x (expected 0x5640)\r\n",
			chip_id_high, chip_id_low);
	if (chip_id_high != 0x56 || chip_id_low != 0x40) {
		diag_write(0xE2U, ((u32)chip_id_high << 8) | chip_id_low);
		xil_printf("ERROR: unexpected camera ID\r\n");
		return XST_FAILURE;
	}
	diag_write(3U, ((u32)chip_id_high << 8) | chip_id_low);

	/*
	 * Initialize Sensor
	 */
	Status = sensor_init(&ps_i2c0);
	if (Status != XST_SUCCESS) {
		diag_write(0xE3U, (u32)Status);
		xil_printf("ERROR: OV5640 initialization failed: %d\r\n", Status);
		return XST_FAILURE;
	}
	diag_write(4U, 0U);
	xil_printf("OV5640 initialization complete\r\n");
	/*
	 * Initialize VDMA driver
	 */
	vdmaConfig = XAxiVdma_LookupConfig(VGA_VDMA_ID);
	if (!vdmaConfig)
	{
		diag_write(0xE4U, VGA_VDMA_ID);
		xil_printf("No video DMA found for ID %d\r\n", VGA_VDMA_ID);

	}
	Status = XAxiVdma_CfgInitialize(&vdma, vdmaConfig, vdmaConfig->BaseAddress);
	if (Status != XST_SUCCESS)
	{
		diag_write(0xE5U, (u32)Status);
		xil_printf("VDMA Configuration Initialization failed %d\r\n", Status);

	}

	/*
	 * Initialize the Display controller and start it
	 */
	Status = DisplayInitialize(&dispCtrl, &vdma, DISP_VTC_ID, DYNCLK_BASEADDR,pFrames, DEMO_STRIDE);
	if (Status != XST_SUCCESS)
	{
		diag_write(0xE6U, (u32)Status);
		xil_printf("Display Ctrl initialization failed during demo initialization%d\r\n", Status);

	}
	Status = DisplayStart(&dispCtrl);
	if (Status != XST_SUCCESS)
	{
		diag_write(0xE7U, (u32)Status);
		xil_printf("Couldn't start display during demo initialization%d\r\n", Status);

	}
	xil_printf("HDMI display started\r\n");
	/* Clear frame buffer */
	memset(dispCtrl.framePtr[dispCtrl.curFrame], 0, 1920 * 1080 * 3);
	/* Start Sensor Vdma */
	Status = vdma_write_init(XPAR_AXIVDMA_1_DEVICE_ID,1280 * 3,720,1280 * 3,(unsigned int)dispCtrl.framePtr[dispCtrl.curFrame]);
	if (Status != XST_SUCCESS) {
		diag_write(0xE8U, (u32)Status);
		return XST_FAILURE;
	}
	diag_write(5U, 0x050002D0U); /* 1280 x 720 */
	sleep(2);
	Xil_DCacheInvalidateRange((INTPTR)dispCtrl.framePtr[dispCtrl.curFrame], 4096U);
	{
		u32 nonzero = 0U;
		for (i = 0; i < 4096; ++i) {
			if (dispCtrl.framePtr[dispCtrl.curFrame][i] != 0U) ++nonzero;
		}
		diag[3] = Xil_In32(XPAR_AXIVDMA_1_BASEADDR + 0x34U);
		diag[4] = nonzero;
		diag[5] = (u32)dispCtrl.framePtr[dispCtrl.curFrame];
		Xil_DCacheFlushRange(DIAG_BASE_ADDR, 24U);
	}
	xil_printf("Camera VDMA started: 1280x720 RGB888\r\n");
	/* 主循环固定 10 ms 一拍：
	 *   GestureLink_Poll() 负责 1 Hz 心跳（"HB\r\n"）
	 * 识别算法接入后，在这里每 33 ms 取一帧，
	 * 算出结果后调用 GestureLink_Send(GESTURE_V / _OPEN / _FIST / _NONE)。
	 */
	{
		XTime tick, last_frame=0, last_poll=0, started, ended;
		u32 last_print=0;
		int last_gesture=-2;
		int calibration_announced=0;
		GestureCommandState command_state;
		tiny_gesture_pipeline_reset();
		gesture_command_init(&command_state);
		xil_printf("TINY_GESTURE_READY ROI=300,0,960,720 / NONE=0 V=1 OPEN=2 FIST=3\r\n");
		xil_printf("AUTO_CALIBRATION: keep ROI empty and still until remaining=0\r\n");
		while (1) {
			gc_poll_keys();
			XTime_GetTime(&tick);
			/* 每秒 100 次调用，保证 HB 计数按 10 ms 一拍走 */
			if (tick - last_poll >= COUNTS_PER_SECOND / 100) {
				last_poll = tick;
				GestureLink_Poll();
			}
			if (tick-last_frame >= COUNTS_PER_SECOND/15) {
		float logits[3]={0,0,0};
				int prediction;
				TinyGestureResult vision;
				GestureCommandInput command_input;
				GestureCommandOutput command_output;
				u32 now=(u32)(tick/(COUNTS_PER_SECOND/1000));
				last_frame=tick;
				XTime_GetTime(&started);
				Xil_DCacheInvalidateRange((INTPTR)pFrames[0],1280U*720U*3U);
				prediction=tiny_gesture_pipeline_process_ex(pFrames[0],DEMO_STRIDE,logits,&vision);
				diag[17]=(u32)tiny_gesture_pipeline_calibration_remaining();
				Xil_DCacheFlushRange(DIAG_BASE_ADDR+68,4);
				if(prediction>=0&&!calibration_announced){
					xil_printf("CALIBRATION_READY: background captured; show gestures now.\r\n");
					calibration_announced=1;
				}else if(prediction<0)calibration_announced=0;
				XTime_GetTime(&ended);
				command_input.raw=prediction<0?GC_GESTURE_NONE:prediction;
				command_input.cx=vision.cx;command_input.cy=vision.cy;
				command_input.touches_edge=vision.touches_edge;
				command_input.calibrating=prediction<0;
				command_output=gesture_command_update(&command_state,&command_input);
				/* Diagnostics readable via JTAG without enabling drone commands. */
				diag[6]=0x5447434E;diag[7]++;diag[8]=(u32)prediction;diag[9]=(u32)command_output.stable;
				diag[10]=(u32)(logits[0]*1000.0f);diag[11]=(u32)(logits[1]*1000.0f);
				diag[12]=(u32)((ended-started)*1000000U/COUNTS_PER_SECOND);diag[13]=now;
				diag[14]=(u32)tiny_gesture_pipeline_last_foreground_pixels();
				diag[15]=(u32)pFrames[0];
				diag[16]=(u32)tiny_gesture_pipeline_last_valid_pixels();
				Xil_DCacheFlushRange(DIAG_BASE_ADDR+24,44);
				/* Continuous CRC frames for the ESP32; legacy G:n remains change-only. */
				if(ResultUartReady){
					gc_send_frame(command_input.raw,command_output.stable);
					gc_send_position(command_input.raw,command_output.stable,vision.cx,vision.cy,command_output.action);
					gc_send_command(command_input.raw,command_output.stable,vision.cx,vision.cy,command_output.action);
					GestureLink_Send((u8)command_output.stable);
				}
				if(prediction<0){
					if(now-last_print>=250){
						xil_printf("TINY_CALIBRATING remaining=%d\r\n",tiny_gesture_pipeline_calibration_remaining());
						last_print=now;
					}
				}else if(last_gesture!=command_output.stable||command_output.changed||now-last_print>=1000){
					xil_printf("TINY_GESTURE raw=%d stable=%d action=%s xy=%d,%d edge=%d logits=%d,%d,%d us=%d\r\n",
						prediction,command_output.stable,gesture_action_name(command_output.action),vision.cx,vision.cy,vision.touches_edge,
						(int)(logits[0]*1000),(int)(logits[1]*1000),(int)(logits[2]*1000),
						(int)((ended-started)*1000000U/COUNTS_PER_SECOND));
					last_gesture=command_output.stable;last_print=now;
				}
			}
			usleep(1000);
		}
	}
}



