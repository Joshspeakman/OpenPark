/* Synthetic fixtures only: loader limits, original-key holds, shared seats. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/walk_mesh.h"
#include "../runtime/walk_rig.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"rig failure line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int read_rig(const char*s,RMRig*r){FILE*f=tmpfile();if(!f)return 0;fputs(s,f);rewind(f);int ok=rm_read(f,r);fclose(f);return ok;}
int main(void){
 RMRig r={0};
 CHECK(!read_rig("TPRIG1 9000 1 1 1 0",&r));
 CHECK(!read_rig("TPRIG1 3 1 1 1 41",&r));
 CHECK(!read_rig("TPRIG1 3 1 1 1 0 nan 0 0 0",&r));
 CHECK(!read_rig("TPRIG1 3 1 1 1 0 0 0 0 1",&r));
 const char*fixture="TPRIG1 3 1 1 2 1\n-1 0 0 0\n1 0 0 0\n0 0 2 0\n0 1 2 12abef\n"
 "10\n1 0 0 0 0 1 0 0 0 0 1 0\n1 0 0 2 0 1 0 3 0 0 1 4\n"
 "65535\n1 0 0 0 0 1 0 0 0 0 1 0\n1 0 0 0 0 1 0 0 0 0 1 0\n";
 CHECK(read_rig(fixture,&r));CHECK(r.np==2&&r.ns==1);
 CHECK(rm_pose(&r,10)==rm_pose(&r,10));CHECK(!rm_pose(&r,11));CHECK(rm_pose(&r,65535));
 float p[3];rm_point(r.p[0].matrices+12,1,2,3,p);CHECK(p[0]==3&&p[1]==5&&p[2]==7);
 uint8_t g[36]={6,7},out[40];CHECK(rm_seat_order(g,2,4,5,0,out));
 CHECK(out[0]==7&&out[1]==6&&out[2]==255&&out[3]==255);
 CHECK(rm_seat_order(g,2,4,0,1,out));CHECK(out[0]==255&&out[1]==7&&out[2]==6&&out[3]==255);
 CHECK(!rm_seat_order(g,2,0,0,0,out));CHECK(!rm_seat_order(g,2,4,8,0,out));
 for(int n=0;n<=36;n++)for(int cap=1;cap<=40;cap++)for(int phase=0;phase<cap;phase++) {
  for(int i=0;i<n;i++)g[i]=(uint8_t)i;
  CHECK(rm_seat_order(g,n,cap,0,(unsigned)phase,out));int found[36]={0},count=0;
  for(int i=0;i<cap;i++)if(out[i]!=255){CHECK(out[i]<n);CHECK(!found[out[i]]++);count++;}
  CHECK(count==(n<cap?n:cap));for(int i=0;i<n-cap;i++)CHECK(!found[i]);
 }
 uint32_t pixels[64*64]={0},pal[256]={0};uint16_t tex[1]={0};float depth[64*64];
 for(int i=0;i<64*64;i++)depth[i]=FLT_MAX;
 WMRender wr={.pixels=pixels,.depth=depth,.w=64,.h=64,.cy=-4,.cz=1,.fy=1,.rx=1,.uz=1,.focal=64,.palette=pal,.texture=tex,.tw=1,.th=1};
 rm_draw(&wr,&r,&r.p[0],0,0,1,NULL);
 CHECK(pixels[32*64+32]==0xffefab12u);CHECK(wr.palette==pal&&wr.texture==tex);
 free(r.v);free(r.t);free(r.p);free(r.matrices);
 puts("rig: bounded loading, exact frame keys, shared seated pose, seat identity/rotation/capacity and raster passed");return 0;
}
