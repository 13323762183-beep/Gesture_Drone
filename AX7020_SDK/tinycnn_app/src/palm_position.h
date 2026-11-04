#ifndef PALM_POSITION_H
#define PALM_POSITION_H
/* Coordinates in the 160x120 recognition ROI. No flight commands. */
static const char *palm_position(int raw,int stable,int x,int y){
 if(raw!=2||stable!=2||x<0||x>=160||y<0||y>=120)return "NONE";
 /* Center dead zone: x=56..103, y=42..77. Diagonals use dominant axis. */
 int dx=x-80,dy=y-60;
 int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
 if(x>=56&&x<104&&y>=42&&y<78)return "CENTER";
 if(ax*120>=ay*160)return dx<0?"LEFT":"RIGHT";
 return dy<0?"UP":"DOWN";
}
#endif
