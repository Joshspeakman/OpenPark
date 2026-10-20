/* Synthetic layered assets: no game data. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/walk_mesh.h"
#include "../runtime/walk_rig.h"
#include "../runtime/walk_layer.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"layer test %d: %s\n",__LINE__,#x);return 1;}}while(0)
static int read_text(const char*s,LPLayer*l){FILE*f=tmpfile();if(!f)return 0;fputs(s,f);rewind(f);int ok=lp_read(f,l);fclose(f);return ok;}
static void clear(uint32_t*p,float*d){for(int i=0;i<64*64;i++){p[i]=0;d[i]=FLT_MAX;}}
int main(void){
 LPLayer l={0};
 CHECK(!read_text("TPLAYER1 99999 1 1 1 1 1 1 0 0 1 1",&l));
 CHECK(!read_text("TPLAYER1 4 2 1 1 1 0 1 0 0 1 1",&l));
 CHECK(!read_text("TPLAYER1 4 2 1 1 1 1 1 nan 0 1 1",&l));
 CHECK(!read_text("TPLAYER1 4 2 1 1 1 1 1 0 0 1 1\n257",&l));
 CHECK(read_text("TPLAYER1 4 2 1 2 1 1 1 0 -1 1 1\n17\n"
 "-1 0 0 0 0 0\n1 0 0 1 0 0\n1 0 1 1 1 0\n-1 0 1 0 1 0\n0 1 2\n0 2 3\n"
 "100\n1 0 0 0 0 1 0 0 0 0 1 0\n1 0 0 0 0 1 0 1 0 0 1 0\n"
 "101\n1 0 0 0 0 1 0 0 0 0 1 0\n1 0 0 0 0 1 0 1 0 0 1 .25\n",&l));
 CHECK(lp_pose(&l,100)&&lp_pose(&l,101)&&!lp_pose(&l,102));
 LPRider riders[2]={{0,8,16,0,3},{0,8,16,0,7}};
 CHECK(lp_valid_riders(&l,riders,0));CHECK(lp_valid_riders(&l,riders,1));CHECK(!lp_valid_riders(&l,riders,2));riders[0].slot=1;CHECK(!lp_valid_riders(&l,riders,1));riders[0].slot=0;
 uint32_t px[64*64],pal[256];float depth[64*64];uint16_t tex[8*16];
 for(int i=0;i<256;i++)pal[i]=0xff000000u|i*65793u;for(int i=0;i<128;i++)tex[i]=91;
 WMRender r={.pixels=px,.depth=depth,.w=64,.h=64,.cy=-4,.cz=1,.fy=1,.rx=1,.uz=1,.focal=64,.palette=pal};
 clear(px,depth);lp_draw(&r,&l,lp_pose(&l,100),0,0,1,riders,1,tex,.125,.125);
 CHECK(px[40*64+32]==pal[17]); /* lower guest is behind solid wall */
 CHECK(px[24*64+32]==pal[91]); /* upper guest emerges above it */
 clear(px,depth);lp_draw(&r,&l,lp_pose(&l,100),0,0,1,riders,0,tex,.125,.125);
 CHECK(px[24*64+32]==0); /* no stale guest in an empty ride */
 clear(px,depth);lp_draw(&r,&l,lp_pose(&l,101),0,0,1,riders,1,tex,.125,.125);
 CHECK(px[18*64+32]==pal[91]); /* source-keyed foot translation */
 CHECK(l.ox==0&&l.oy==-1&&l.hx==1&&l.hy==1);lp_free(&l);
 puts("layered ride: bounded loading, source frame keys, fixed footprint, rider slots, occlusion, empty seats and pose translation passed");return 0;
}
