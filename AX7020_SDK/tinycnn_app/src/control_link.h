#ifndef CONTROL_LINK_H
#define CONTROL_LINK_H

#include "gesture_command.h"

/* AX7020 schematic: PS KEY1=MIO50, PS KEY2=MIO51, active low. */
static unsigned gc_seq=0,gc_keys=0,gc_prev=0,gc_stable=0;
static unsigned gc_edge[2]={0,0};
static unsigned gc_now(void){XTime t;XTime_GetTime(&t);return (unsigned)(t/(COUNTS_PER_SECOND/1000));}
static void gc_key_init(void){
 unsigned mask=(1U<<18)|(1U<<19);int p;
 Xil_Out32(0xF8000008,0xDF0D);
 for(p=50;p<=51;p++){unsigned a=0xF8000700+4*p;Xil_Out32(a,(Xil_In32(a)&~0xFEU)|1U);}
 Xil_Out32(0xF8000004,0x767B);
 Xil_Out32(0xE000A244,Xil_In32(0xE000A244)&~mask);
 Xil_Out32(0xE000A248,Xil_In32(0xE000A248)&~mask);
}
static void gc_poll_keys(void){
 unsigned now=gc_now(),v=(~Xil_In32(0xE000A064)>>18)&3U;int i;
 for(i=0;i<2;i++){
  unsigned bit=1U<<i;
  if((v&bit)!=(gc_prev&bit)){gc_prev=(gc_prev&~bit)|(v&bit);gc_edge[i]=now;}
  if(now-gc_edge[i]>=30)gc_stable=(gc_stable&~bit)|(v&bit);
 }
 gc_keys=gc_stable;
}
static unsigned short gc_crc16(const char *s){
 unsigned short c=0xffff;int k;
 while(*s){c^=(unsigned short)(unsigned char)*s++<<8;for(k=0;k<8;k++)c=c&0x8000?(c<<1)^0x1021:c<<1;}return c;
}
static void gc_send_payload(const char *payload){
 char line[128];
 snprintf(line,sizeof(line),"%s*%04X\r\n",payload,gc_crc16(payload));
 result_uart_send(line);
}
static unsigned gc_send_frame(int raw,int stable){
 char payload[64];
 ++gc_seq;
 snprintf(payload,sizeof(payload),"GC1,%u,%u,%u,%u",gc_seq,(unsigned)raw,(unsigned)stable,gc_keys);
 gc_send_payload(payload);
 return gc_seq;
}
/* Compatibility position frame for the existing ESP32 monitor. */
static void gc_send_position(int raw,int stable,int x,int y,GestureAction action){
 char payload[80];const char *pos="NONE";
 if(raw==GC_GESTURE_OPEN&&stable==GC_GESTURE_OPEN){
  if(action==GC_ACTION_LEFT)pos="LEFT";
  else if(action==GC_ACTION_RIGHT)pos="RIGHT";
  else if(action==GC_ACTION_FORWARD)pos="UP";
  else if(action==GC_ACTION_BACK)pos="DOWN";
  else if(action==GC_ACTION_HOLD)pos="CENTER";
 }
 if(!strcmp(pos,"NONE"))x=y=-1;
 snprintf(payload,sizeof(payload),"GP1,%u,%d,%d,%s",gc_seq,x,y,pos);
 gc_send_payload(payload);
}
/* New command frame: continuous abstract output; it never drives a motor itself. */
static void gc_send_command(int raw,int stable,int x,int y,GestureAction action){
 char payload[112];
 snprintf(payload,sizeof(payload),"CMD1,%u,%s,%u,%u,%d,%d",gc_seq,
          gesture_action_name(action),(unsigned)raw,(unsigned)stable,x,y);
 gc_send_payload(payload);
}

#endif
