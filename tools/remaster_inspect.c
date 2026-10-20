/* Native canonical-model inspection; no game data or app installation needed.
 * Produces four camera captures for every source-keyed pose. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include "../runtime/walk_mesh.h"
#include "../runtime/walk_rig.h"
static void be32(FILE*f,uint32_t n){fputc(n>>24,f);fputc(n>>16,f);fputc(n>>8,f);fputc(n,f);}
static void chunk(FILE*f,const char*t,const uint8_t*p,size_t n){
    be32(f,(uint32_t)n);fwrite(t,1,4,f);if(n)fwrite(p,1,n,f);
    uint32_t c=(uint32_t)crc32(0,(const Bytef*)t,4);if(n)c=(uint32_t)crc32(c,p,(uInt)n);be32(f,c);
}
static int png(const char*path,const uint32_t*p,int w,int h){
    size_t n=(size_t)h*(w*4+1);uint8_t*raw=malloc(n);if(!raw)return 0;
    size_t at=0;for(int y=0;y<h;y++){raw[at++]=0;for(int x=0;x<w;x++){
        uint32_t c=p[y*w+x];raw[at++]=c;raw[at++]=c>>8;raw[at++]=c>>16;raw[at++]=c>>24;}}
    uLongf zn=compressBound(n);uint8_t*z=malloc(zn);if(!z){free(raw);return 0;}
    if(compress2(z,&zn,raw,n,6)!=Z_OK){free(raw);free(z);return 0;}free(raw);
    FILE*f=fopen(path,"wb");if(!f){free(z);return 0;}
    fwrite("\x89PNG\r\n\x1a\n",1,8,f);
    uint8_t ih[13]={w>>24,w>>16,w>>8,w,h>>24,h>>16,h>>8,h,8,6,0,0,0};
    chunk(f,"IHDR",ih,13);chunk(f,"IDAT",z,zn);chunk(f,"IEND",NULL,0);free(z);
    int bad=ferror(f);if(fclose(f))bad=1;return !bad;
}
int main(int argc,char**argv){
    if(argc!=3){fprintf(stderr,"usage: remaster_inspect MODEL OUT_PREFIX\n");return 2;}
    FILE*f=fopen(argv[1],"r");if(!f)return 3;
    RMRig*r=&rm_cache[0];rm_count=1;int ok=rm_read(f,r);fclose(f);if(!ok){rm_reset();return 3;}
    const int w=640,h=640;uint32_t*p=malloc(w*h*4);float*d=malloc(w*h*sizeof*d);
    if(!p||!d){free(p);free(d);rm_reset();return 4;}
    /* Fix the camera across the whole animation; per-frame auto-framing would
     * hide translation errors and turn changing bounds into apparent zoom. */
    float all_lo[3]={FLT_MAX,FLT_MAX,FLT_MAX},all_hi[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for(int pose=0;pose<r->np;pose++)for(int i=0;i<r->nv;i++){
        const RMVertex*v=&r->v[i];float q[3];rm_point(r->p[pose].matrices+v->bone*12,v->x,v->y,v->z,q);
        for(int k=0;k<3;k++){all_lo[k]=fminf(all_lo[k],q[k]);all_hi[k]=fmaxf(all_hi[k],q[k]);}
    }
    float mx=(all_lo[0]+all_hi[0])*.5f,my=(all_lo[1]+all_hi[1])*.5f,mz=(all_lo[2]+all_hi[2])*.5f;
    float radius=.5f*sqrtf(powf(all_hi[0]-all_lo[0],2)+powf(all_hi[1]-all_lo[1],2)+powf(all_hi[2]-all_lo[2],2));
    float distance=fmaxf(.5f,radius)*2.7f;
    /* Seat-local poses and track undersides can extend below their origin.
     * Keep the measurement floor beneath them instead of hiding that art. */
    float floor_z=fminf(0,all_lo[2])-.003f;
    for(int pose=0;pose<r->np;pose++){
        float lo[3]={FLT_MAX,FLT_MAX,FLT_MAX},hi[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for(int i=0;i<r->nv;i++){
            const RMVertex*v=&r->v[i];float q[3];rm_point(r->p[pose].matrices+v->bone*12,v->x,v->y,v->z,q);
            for(int k=0;k<3;k++){lo[k]=fminf(lo[k],q[k]);hi[k]=fmaxf(hi[k],q[k]);}
        }
        for(int view=0;view<4;view++){
            double yaw=(-71.565+view*90)*M_PI/180,pitch=.32;
            WMRender wr={.pixels=p,.depth=d,.w=w,.h=h,
                .cx=mx-cos(yaw)*cos(pitch)*distance,.cy=my-sin(yaw)*cos(pitch)*distance,.cz=mz+sin(pitch)*distance,
                .fx=cos(yaw)*cos(pitch),.fy=sin(yaw)*cos(pitch),.fz=-sin(pitch),
                .rx=-sin(yaw),.ry=cos(yaw),.ux=cos(yaw)*sin(pitch),.uy=sin(yaw)*sin(pitch),.uz=cos(pitch),.focal=650};
            for(int i=0;i<w*h;i++){p[i]=0xff332b24;d[i]=FLT_MAX;}
            /* Neutral measurement floor, not a game asset. Quarter-cell tiles
             * make ground contact and relative dimensions visible. */
            uint32_t floor_colour;wr.rgba=&floor_colour;wr.tw=wr.th=1;
            int x0=(int)floorf(all_lo[0]*4)-2,x1=(int)ceilf(all_hi[0]*4)+2;
            int y0=(int)floorf(all_lo[1]*4)-2,y1=(int)ceilf(all_hi[1]*4)+2;
            for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){
                floor_colour=((x+y)&1)?0xff484139:0xff453e36;
                wm_quad(&wr,wm_camera(&wr,x*.25f,y*.25f,floor_z,0,0),
                    wm_camera(&wr,(x+1)*.25f,y*.25f,floor_z,0,0),
                    wm_camera(&wr,(x+1)*.25f,(y+1)*.25f,floor_z,0,0),
                    wm_camera(&wr,x*.25f,(y+1)*.25f,floor_z,0,0));
            }
            wr.rgba=NULL;
            unsigned floor_fragments=wr.fragments;
            rm_draw(&wr,r,&r->p[pose],0,0,1,NULL);
            char path[4096];snprintf(path,sizeof path,"%s-%u-%d.png",argv[2],r->p[pose].frame,view);
            if(wr.fragments==floor_fragments||!png(path,p,w,h)){free(p);free(d);rm_reset();return 5;}
        }
        printf("pose %u bounds %.4f %.4f %.4f / %.4f %.4f %.4f; %d triangles\n",
            r->p[pose].frame,lo[0],lo[1],lo[2],hi[0],hi[1],hi[2],r->nt);
    }
    free(p);free(d);rm_reset();return 0;
}
