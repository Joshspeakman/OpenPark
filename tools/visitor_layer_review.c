/* Replay captured native thing arrays through the actual layered ride path.
 * Owned-game inputs and outputs must stay under ignored work/. */
#define main visitor_snapshot_main
#include "visitor_snapshot.c"
#undef main
int main(int argc,char**argv){
 if(argc!=9){fprintf(stderr,"usage: review BASE PALETTE CAPTUREDIR OUT THING COUNT YAW PROFILE\n");return 2;}
 memory=malloc(64u<<20);FILE*f=fopen(argv[1],"rb");if(!memory||!f||fread(memory,1,64u<<20,f)!=(64u<<20))return 2;fclose(f);M=memory;
 uint8_t pal[768];f=fopen(argv[2],"rb");if(!f||fread(pal,1,768,f)!=768)return 2;fclose(f);
 for(int i=0;i<256;i++)palette[i]=0xff000000u|(pal[i*3]*255/63)|((pal[i*3+1]*255/63)<<8)|((pal[i*3+2]*255/63)<<16);
 int thing=atoi(argv[5]),count=atoi(argv[6]);double yaw=atof(argv[7])*M_PI/180;
 if(thing<1||thing>=12000||count<1||count>1000)return 2;
 setenv("TP_DATA",argv[8],1);setenv("TP_ART_MODE","faithful",1);tp_map_w=tp_map_ext=128;
 uint32_t base=r32(THINGS),bytes=r32(0x1d3b78)-base+200;if(base+(uint64_t)bytes>(64u<<20))return 2;
 char path[4096];snprintf(path,sizeof path,"%s/evidence.csv",argv[4]);FILE*log=fopen(path,"w");if(!log)return 2;
 fprintf(log,"sample,frame,running,speed,riders,variants,width,height,anchor_x,anchor_y,model,rendered_riders,world_x,world_y,triangles\n");
 uint32_t*img=malloc(960*600*4);uint32_t*src=malloc(128*128*4);uint16_t tex[128*128];
 for(int i=0;i<count;i++){
  snprintf(path,sizeof path,"%s/things-%03d.bin",argv[3],i);f=fopen(path,"rb");if(!f||fread(memory+base,1,bytes,f)!=bytes)return 3;fclose(f);
  uint32_t t=base+thing*200;double x=M[t+7]+M[t+6]/256.0,y=M[t+9]+M[t+8]/256.0;
  if(!tp_walk_start(x,y))return 3;tp_walk_frame();Snap*s=&snaps[front];Card c;int found=0;
  for(int k=0;k<s->ncards;k++)if(s->cards[k].idx==thing){c=s->cards[k];found=1;break;}if(!found)return 4;
  int riders=0;unsigned variants=0;for(int rider=(uint16_t)r16(t+71);rider && riders<40;rider=(uint16_t)r16(base+rider*200+71)){
   if(rider>=12000)return 4;riders++;variants|=1u<<M[base+rider*200+89];
  }
  memset(tex,255,sizeof tex);
  for(int yy=0;yy<c.h;yy++)for(int xx=0;xx<c.w;xx++){
   int px=64-c.ax+xx,py=112-c.ay+yy;if(px<0||px>=128||py<0||py>=128)return 4;
   tex[py*128+px]=s->pix[c.off+yy*c.w+xx];
  }
  int cw=c.w,ch=c.h,ax=c.ax,ay=c.ay;
  for(int j=0;j<128*128;j++)src[j]=tex[j]<256?s->pal[tex[j]]:0xff403830;
  snprintf(path,sizeof path,"%s/source-%03d.png",argv[4],i);if(!png(path,src,128,128))return 5;
  // Keep every native texture/rider offset intact; isolate only the ride card.
  s->cards[0]=c;s->ncards=1;
  double dist=getenv("TP_REVIEW_DISTANCE")?atof(getenv("TP_REVIEW_DISTANCE")):4.8;
  W.cam_x=c.x-cos(yaw)*dist;W.cam_y=c.y-sin(yaw)*dist;W.cam_yaw=yaw;
  W.cam_z=getenv("TP_REVIEW_EYE")?atof(getenv("TP_REVIEW_EYE")):1.05;
  W.cam_pitch=getenv("TP_REVIEW_PITCH")?atof(getenv("TP_REVIEW_PITCH")):.03;
  const LPLayer*m=lp_find(c.cls,c.type);const LPPose*pose=m?lp_pose(m,c.frame):NULL;
  int modeled=c.layer_ok && pose && lp_valid_riders(m,c.riders,c.nriders);
  if(!modeled){fprintf(stderr,"Missing compatible layered pose for frame %u (eligible=%u riders=%u pose=%p)\n",c.frame,c.layer_ok,c.nriders,(void*)pose);for(int j=0;j<c.nriders;j++)fprintf(stderr," rider %d: slot=%u size=%ux%u variant=%u\n",j,c.riders[j].slot,c.riders[j].w,c.riders[j].h,c.riders[j].variant);return 4;}
  if(!tp_walk_render(img,960,600))return 5;snprintf(path,sizeof path,"%s/view-%03d.png",argv[4],i);if(!png(path,img,960,600))return 5;
  fprintf(log,"%d,%u,%u,%u,%d,%u,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%d\n",i,(uint16_t)r16(t+10),(uint16_t)r16(t+50),(uint16_t)r16(t+46),riders,variants,cw,ch,ax,ay,modeled,c.nriders,c.x,c.y,m->nt);
 }
 fclose(log);lp_reset();wm_models_reset();free(src);free(img);free(memory);return 0;
}
