/* Bare-metal implementation of the PC skin/contour/hull-defect pipeline.
 * Local test only: output values are protocol NONE=0,V=1,OPEN=2,FIST=3.
 * Static storage avoids heap allocation and large stack frames. */
#ifndef GESTURE_ARM_H
#define GESTURE_ARM_H
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#define GA_W 160
#define GA_H 120
#define GA_N (GA_W*GA_H)
typedef struct { int x,y,i; } GAPoint;
typedef struct { unsigned frames; int raw,stable,area,defects,cx,cy; float openness,ms; } GAResult;
static uint8_t ga_rgb[GA_N][3],ga_blur[GA_N][3],ga_mask[GA_N],ga_tmp[GA_N],ga_seen[GA_N];
static int ga_queue[GA_N];
static GAPoint ga_contour[GA_N],ga_sorted[GA_N],ga_hull[GA_N*2];
static int ga_dx[8]={-1,-1,0,1,1,1,0,-1},ga_dy[8]={0,-1,-1,-1,0,1,1,1};
static GAResult ga_result;
static int ga_contour_n,ga_hull_n,ga_box[4];
static float ga_solidity,ga_extent,ga_maxdepth;
static GAPoint ga_farpoints[64];
static int ga_far_n;
static float ga_ema,ga_present;
static int ga_label,ga_since,ga_initialized;
static int ga_histx[10],ga_histy[10],ga_histlabel[10],ga_histn,ga_histpos;
static unsigned ga_histtime[10],ga_prev_ms;
static int ga_inside(int x,int y){return x>=0&&x<GA_W&&y>=0&&y<GA_H;}
static int ga_cmp(const void *a,const void *b){const GAPoint *p=a,*q=b;return p->x!=q->x?p->x-q->x:p->y-q->y;}
static int ga_cross(GAPoint a,GAPoint b,GAPoint c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
static float ga_clamp(float a){return a<0?0:a>1?1:a;}
/* Ellipse kernels match OpenCV's 3x3 cross and 5x5 ellipse. */
static void ga_morph(const uint8_t *src,uint8_t *dst,int radius,int dilate){
 int x,y,dx,dy;
 for(y=0;y<GA_H;y++)for(x=0;x<GA_W;x++){
  int v=!dilate;
  for(dy=-radius;dy<=radius;dy++)for(dx=-radius;dx<=radius;dx++){
   int xx=x+dx,yy=y+dy;
   if((radius==1&&abs(dx)+abs(dy)>1)||(radius==2&&abs(dy)==2&&dx!=0))continue;
   if(!ga_inside(xx,yy))continue;
   if(dilate)v|=src[yy*GA_W+xx];else v&=src[yy*GA_W+xx];
  } dst[y*GA_W+x]=(uint8_t)v;
 }
}
static int ga_extract(void){
 int i,j,best=0,seed=-1,head,tail,start,x,y,bx,by,firstx=-1,firsty=-1,n=0;
 memset(ga_seen,0,sizeof(ga_seen));
 /* 8-neighbour largest component, matching external contour connectivity. */
 for(i=0;i<GA_N;i++)if(ga_mask[i]&&!ga_seen[i]){
  head=0;tail=1;ga_queue[0]=i;ga_seen[i]=1;
  while(head<tail){int p=ga_queue[head++];x=p%GA_W;y=p/GA_W;
   for(j=0;j<8;j++){int xx=x+ga_dx[j],yy=y+ga_dy[j],k=yy*GA_W+xx;
    if(ga_inside(xx,yy)&&ga_mask[k]&&!ga_seen[k]){ga_seen[k]=1;ga_queue[tail++]=k;}
   }
  } if(tail>best){best=tail;seed=i;}
 }
 if(seed<0)return 0;
 /* Trace the exterior using Moore neighbours, clockwise. */
 start=seed;x=start%GA_W;y=start/GA_W;bx=x-1;by=y;
 for(i=0;i<GA_N;i++){
  int b=0,found=-1,nx,ny;
  ga_contour[n].x=x;ga_contour[n].y=y;ga_contour[n].i=n;n++;
  for(j=0;j<8;j++)if(x+ga_dx[j]==bx&&y+ga_dy[j]==by){b=j;break;}
  for(j=1;j<=8;j++){int d=(b+j)%8;nx=x+ga_dx[d];ny=y+ga_dy[d];
   if(ga_inside(nx,ny)&&ga_mask[ny*GA_W+nx]){found=d;break;}
  }
  if(found<0)break;
  nx=x+ga_dx[found];ny=y+ga_dy[found];
  if(firstx<0){firstx=nx;firsty=ny;}
  else if(x==start%GA_W&&y==start/GA_W&&nx==firstx&&ny==firsty){n--;break;}
  bx=x+ga_dx[(found+7)%8];by=y+ga_dy[(found+7)%8];x=nx;y=ny;
 }
 return n;
}
static void ga_shape(int n){
 int i,k=0,t,h;double a=0,sx=0,sy=0,ha=0;float s,f;
 ga_result.area=0;ga_result.defects=0;ga_result.cx=ga_result.cy=0;ga_result.openness=0;ga_result.raw=0;
 ga_contour_n=n;ga_hull_n=ga_far_n=0;ga_solidity=ga_extent=ga_maxdepth=0;
 memset(ga_box,0,sizeof(ga_box));
 if(n<5)return;
 for(i=0;i<n;i++){GAPoint p=ga_contour[i],q=ga_contour[(i+1)%n];double c=p.x*q.y-q.x*p.y;a+=c;sx+=(p.x+q.x)*c;sy+=(p.y+q.y)*c;}
 ga_result.area=(int)(fabs(a)*0.5);
 if(ga_result.area<346||fabs(a)<1)return; /* max(1.5%,1.8%) of 19200 */
 ga_result.cx=(int)(sx/(3*a));ga_result.cy=(int)(sy/(3*a));
 {int minx=GA_W,miny=GA_H,maxx=0,maxy=0;
  for(i=0;i<n;i++){GAPoint p=ga_contour[i];if(p.x<minx)minx=p.x;if(p.y<miny)miny=p.y;if(p.x>maxx)maxx=p.x;if(p.y>maxy)maxy=p.y;}
  ga_box[0]=minx;ga_box[1]=miny;ga_box[2]=maxx-minx+1;ga_box[3]=maxy-miny+1;
  ga_extent=(float)ga_result.area/(ga_box[2]*ga_box[3]);
 }
 memcpy(ga_sorted,ga_contour,n*sizeof(GAPoint));qsort(ga_sorted,n,sizeof(GAPoint),ga_cmp);
 for(i=0;i<n;i++){while(k>=2&&ga_cross(ga_hull[k-2],ga_hull[k-1],ga_sorted[i])<=0)k--;ga_hull[k++]=ga_sorted[i];}
 for(i=n-2,t=k+1;i>=0;i--){while(k>=t&&ga_cross(ga_hull[k-2],ga_hull[k-1],ga_sorted[i])<=0)k--;ga_hull[k++]=ga_sorted[i];}
 h=k-1;if(h<3)return;
 for(i=0;i<h;i++){GAPoint p=ga_hull[i],q=ga_hull[(i+1)%h];ha+=p.x*q.y-q.x*p.y;}
 if(fabs(ha)<1)return;
 ga_hull_n=h;ga_solidity=(float)(fabs(a)/fabs(ha));
 /* Put hull vertices back into contour order; find deepest point per edge. */
 for(i=1;i<h;i++){GAPoint p=ga_hull[i];int j=i;while(j>0&&ga_hull[j-1].i>p.i){ga_hull[j]=ga_hull[j-1];j--;}ga_hull[j]=p;}
 for(i=0;i<h;i++){
  GAPoint p=ga_hull[i],q=ga_hull[(i+1)%h],far=p;int j=(p.i+1)%n;float maxd=0;
  float dx=q.x-p.x,dy=q.y-p.y,len=sqrtf(dx*dx+dy*dy),limit=0.06f*sqrtf((float)ga_result.area);
  if(limit<2)limit=2;
  if(len<1)continue;
  while(j!=q.i){float d=fabsf(dx*(p.y-ga_contour[j].y)-(p.x-ga_contour[j].x)*dy)/len;
   if(d>maxd){maxd=d;far=ga_contour[j];}j=(j+1)%n;
  }
  if(maxd>=limit){float ax=p.x-far.x,ay=p.y-far.y,bx=q.x-far.x,by=q.y-far.y;
   float den=sqrtf((ax*ax+ay*ay)*(bx*bx+by*by));
   if(den>0&&(ax*bx+ay*by)/den>=-0.08715574f){
    ga_result.defects++;
    if(ga_far_n<64)ga_farpoints[ga_far_n++]=far;
    if(maxd>ga_maxdepth)ga_maxdepth=maxd;
   }
  }
 }
 s=ga_clamp((0.85f-(float)(fabs(a)/fabs(ha)))/0.25f);
 f=ga_clamp(ga_result.defects/4.0f);
 ga_result.openness=ga_clamp(0.15f*s+0.85f*f);
 ga_result.raw=ga_result.openness>=0.70f?2:ga_result.openness>=0.25f?1:3;
}
static GAResult *GestureARM_Process(const uint8_t *frame,int stride,unsigned now_ms){
 int x,y,c,i,n;float present;int lab;float mx=0,my=0,var=0;int count=0;
 /* Resize ROI (300,0,960,720) to 160x120: exact 6x6 area average.
  * Verified against aligned HDMI capture: VDMA memory bytes are B,G,R.
  * AXI bit remap alone was insufficient to infer the end-to-end order. */
 for(y=0;y<GA_H;y++)for(x=0;x<GA_W;x++){
  int sum[3]={0,0,0},xx,yy;
  for(yy=0;yy<6;yy++)for(xx=0;xx<6;xx++){const uint8_t *p=frame+(y*6+yy)*stride+(300+x*6+xx)*3;for(c=0;c<3;c++)sum[c]+=p[c];}
  for(c=0;c<3;c++)ga_rgb[y*GA_W+x][c]=(sum[c]+18)/36;
 }
 for(y=0;y<GA_H;y++)for(x=0;x<GA_W;x++)for(c=0;c<3;c++){
  int dx,dy,sum=0;
  for(dy=-1;dy<=1;dy++)for(dx=-1;dx<=1;dx++){
   int xx=x+dx,yy=y+dy,w=(dx==0?2:1)*(dy==0?2:1);
   if(xx<0)xx=1;
   if(xx>=GA_W)xx=GA_W-2;
   if(yy<0)yy=1;
   if(yy>=GA_H)yy=GA_H-2;
   sum+=w*ga_rgb[yy*GA_W+xx][c];
  }ga_blur[y*GA_W+x][c]=(sum+8)/16;
 }
 for(i=0;i<GA_N;i++){
  int r=ga_blur[i][2],g=ga_blur[i][1],b=ga_blur[i][0];
  int Y=(4899*r+9617*g+1868*b+8192)>>14;
  int cr=(((r-Y)*11682+8192)>>14)+128,cb=(((b-Y)*9241+8192)>>14)+128;
  int hi=r>g?r:g,lo=r<g?r:g,sat; if(b>hi)hi=b;if(b<lo)lo=b;sat=hi?(255*(hi-lo)+hi/2)/hi:0;
  ga_mask[i]=(Y>=55&&cr>=130&&cr<=185&&cb>=75&&cb<=135&&sat>=15);
 }
 ga_morph(ga_mask,ga_tmp,1,0);ga_morph(ga_tmp,ga_mask,1,1);
 ga_morph(ga_mask,ga_tmp,2,1);ga_morph(ga_tmp,ga_mask,2,0);
 n=ga_extract();ga_shape(n);present=ga_result.raw!=0;
 if(!ga_initialized){ga_ema=ga_result.openness;ga_present=present;ga_initialized=1;}
 else{
  /* PC alpha=0.30 at 30fps; retain the same wall-clock response at ~15fps. */
  float dt=(float)(now_ms-ga_prev_ms);float alpha;
  if(dt>250)dt=250;
  alpha=1.0f-powf(0.7f,dt*0.03f);
  ga_ema=alpha*ga_result.openness+(1-alpha)*ga_ema;
  ga_present=alpha*present+(1-alpha)*ga_present;
 }
 ga_prev_ms=now_ms;
 lab=ga_present<0.35f?0:ga_ema>=0.70f?2:ga_ema>=0.25f?1:3;
 if(lab!=ga_label){ga_label=lab;ga_since=now_ms;}
 ga_histx[ga_histpos]=ga_result.cx;ga_histy[ga_histpos]=ga_result.cy;ga_histlabel[ga_histpos]=lab;ga_histtime[ga_histpos]=now_ms;ga_histpos=(ga_histpos+1)%10;if(ga_histn<10)ga_histn++;
 for(i=0;i<ga_histn;i++)if(ga_histlabel[i]==lab&&now_ms-ga_histtime[i]<=333){mx+=ga_histx[i];my+=ga_histy[i];count++;}
 if(count){mx/=count;my/=count;for(i=0;i<ga_histn;i++)if(ga_histlabel[i]==lab&&now_ms-ga_histtime[i]<=333){float dx=ga_histx[i]-mx,dy=ga_histy[i]-my;var+=dx*dx+dy*dy;}var/=count;}
 if(now_ms-(unsigned)ga_since>=400&&var<=36)ga_result.stable=lab;
 ga_result.frames++;return &ga_result;
}

/* Immutable debug snapshot requested by the PC, not a PC recognition result.
 * CPU and debugger exchange data through an uncached-on-read DDR mailbox.
 * Camera and algorithm keep running while JTAG reads the published snapshot. */
#define GA_MAIL_ADDR 0x03000000U
#define GA_SNAP_ADDR 0x03000100U
#define GA_SNAP_BYTES (128+GA_N*5)
static uint8_t * const ga_snap=(uint8_t *)GA_SNAP_ADDR;
static void ga_mark(int x,int y,int v){if(ga_inside(x,y))ga_snap[128+GA_N*4+y*GA_W+x]=(uint8_t)v;}
static void ga_line(int x,int y,int xx,int yy,int v){
 int dx=abs(xx-x),sx=x<xx?1:-1,dy=-abs(yy-y),sy=y<yy?1:-1,err=dx+dy;
 for(;;){int e;ga_mark(x,y,v);if(x==xx&&y==yy)break;e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
}
static void GestureARM_Publish(unsigned now_ms){
 volatile uint32_t *mail=(volatile uint32_t *)GA_MAIL_ADDR;
 uint32_t *hdr=(uint32_t *)ga_snap;
 unsigned request;int i;
 Xil_DCacheInvalidateRange(GA_MAIL_ADDR,64);
 request=mail[0];if(!request||request==mail[1])return;
 memset(ga_snap,0,128);
 hdr[0]=0x41524D47;hdr[1]=2;hdr[2]=request;hdr[3]=ga_result.frames;
 hdr[4]=ga_result.raw;hdr[5]=ga_result.stable;hdr[6]=ga_result.area;hdr[7]=ga_result.defects;
 hdr[8]=ga_result.cx;hdr[9]=ga_result.cy;hdr[10]=(unsigned)(ga_result.openness*1000);
 hdr[11]=(unsigned)(ga_ema*1000);hdr[12]=(unsigned)(ga_present*1000);hdr[13]=(unsigned)(ga_result.ms*1000);
 hdr[14]=now_ms;hdr[15]=(unsigned)(ga_solidity*1000);hdr[16]=(unsigned)(ga_extent*1000);hdr[17]=(unsigned)(ga_maxdepth*1000);
 for(i=0;i<4;i++)hdr[18+i]=ga_box[i];
 hdr[22]=Xil_In32(0x02000038U);hdr[23]=GA_W;hdr[24]=GA_H;hdr[25]=ga_contour_n;hdr[26]=ga_hull_n;
 memcpy(ga_snap+128,ga_rgb,GA_N*3);
 memcpy(ga_snap+128+GA_N*3,ga_mask,GA_N);
 memset(ga_snap+128+GA_N*4,0,GA_N);
 for(i=0;i<ga_contour_n;i++)ga_mark(ga_contour[i].x,ga_contour[i].y,1);
 for(i=0;i<ga_hull_n;i++){GAPoint a=ga_hull[i],b=ga_hull[(i+1)%ga_hull_n];ga_line(a.x,a.y,b.x,b.y,2);}
 if(ga_box[2]){int x=ga_box[0],y=ga_box[1],xx=x+ga_box[2]-1,yy=y+ga_box[3]-1;
  ga_line(x,y,xx,y,3);ga_line(xx,y,xx,yy,3);ga_line(xx,yy,x,yy,3);ga_line(x,yy,x,y,3);
  ga_mark(ga_result.cx,ga_result.cy,4);
 }
 for(i=0;i<ga_far_n;i++)ga_mark(ga_farpoints[i].x,ga_farpoints[i].y,5);
 Xil_DCacheFlushRange(GA_SNAP_ADDR,GA_SNAP_BYTES);
 mail[1]=request;Xil_DCacheFlushRange(GA_MAIL_ADDR,64);
}
#endif
