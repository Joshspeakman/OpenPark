/* Offline rig review using the same native rasterizer as Visitor View.
 * This is an asset fixture, not a live game or a timing verification. */
#define main visitor_snapshot_main
#include "visitor_snapshot.c"
#undef main
int main(int argc,char**argv){
 if(argc!=4){fprintf(stderr,"usage: rig_review profile output-directory guest-variant\n");return 2;}
 setenv("TP_DATA",argv[1],1);const RMRig*r=rm_find(2,atoi(argv[3]));if(!r)return 3;
 const int w=800,h=500;uint32_t*p=malloc(w*h*4);float*d=malloc(w*h*sizeof*d);if(!p||!d)return 3;
 for(int pose=0;pose<r->np;pose++)for(int side=0;side<4;side++){
  double yaw=(-65+side*90)*M_PI/180,pitch=.12;WMRender wr={.pixels=p,.depth=d,.w=w,.h=h,.cx=(float)(-cos(yaw)*2),.cy=(float)(-sin(yaw)*2),.cz=.63,
   .fx=cos(yaw)*cos(pitch),.fy=sin(yaw)*cos(pitch),.fz=-sin(pitch),.rx=-sin(yaw),.ry=cos(yaw),.ux=cos(yaw)*sin(pitch),.uy=sin(yaw)*sin(pitch),.uz=cos(pitch),.focal=750};
  for(int i=0;i<w*h;i++){p[i]=0xff383128;d[i]=FLT_MAX;}
  rm_draw(&wr,r,&r->p[pose],0,0,1,NULL);
  if(!wr.fragments)return 4;char path[4096];snprintf(path,sizeof path,"%s/guest-%s-frame-%u-side-%d.png",argv[2],argv[3],r->p[pose].frame,side);
  if(!png(path,p,w,h))return 5;
 }
 printf("variant %s: %d poses, four viewpoints, same walking/seated geometry\n",argv[3],r->np);
 rm_reset();free(p);free(d);return 0;
}
