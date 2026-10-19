/* Offline visual inspection of a copied TP_DUMP memory snapshot. No game runs,
 * no saves are written. Include the private walk implementation to inspect its
 * snapshots and construct an isolated frame gallery without a public game API.
 * cc -O2 -I runtime tools/visitor_snapshot.c runtime/native_land.c
 *    runtime/native_draw.c -lz -lm -lpthread -o work/visitor-3d/native/snapshot
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth x y yaw [original]
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth frame FRAME SHAPE yaw
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../runtime/walk.c"
static uint8_t *memory;static uint32_t palette[256];
uint32_t tp_hook_pcs[32],tp_map_w=128,tp_map_ext=128;
uint8_t *tp_host_memory(void){return memory;}
void tp_host_palette_rgba(uint32_t p[256]){memcpy(p,palette,sizeof palette);}
void tp_hooks_changed(void){}
static void be32(FILE*f,uint32_t n){fputc(n>>24,f);fputc(n>>16,f);fputc(n>>8,f);fputc(n,f);}
static void chunk(FILE*f,const char*t,const uint8_t*p,size_t n){be32(f,(uint32_t)n);fwrite(t,1,4,f);if(n)fwrite(p,1,n,f);uint32_t c=(uint32_t)crc32(0,(const Bytef*)t,4);c=(uint32_t)crc32(c,p,(uInt)n);be32(f,c);}
static int png(const char*path,uint32_t*p,int w,int h){
    size_t stride=1+(size_t)w*3,n=stride*h;uint8_t*raw=malloc(n);if(!raw)return 0;
    for(int y=0;y<h;y++){raw[y*stride]=0;for(int x=0;x<w;x++){uint32_t c=p[y*w+x];size_t i=y*stride+1+x*3;raw[i]=c;raw[i+1]=c>>8;raw[i+2]=c>>16;}}
    uLongf zn=compressBound(n);uint8_t*z=malloc(zn);if(!z){free(raw);return 0;}compress2(z,&zn,raw,n,6);
    FILE*f=fopen(path,"wb");if(!f){free(z);free(raw);return 0;}fwrite("\x89PNG\r\n\x1a\n",1,8,f);
    uint8_t head[13]={w>>24,w>>16,w>>8,w,h>>24,h>>16,h>>8,h,8,2,0,0,0};chunk(f,"IHDR",head,13);chunk(f,"IDAT",z,zn);chunk(f,"IEND",NULL,0);fclose(f);free(z);free(raw);return 1;
}
int main(int argc,char**argv){
    if(argc<8){fprintf(stderr,"usage: snapshot memory palette output mapwidth x y yaw [original] | ... frame FRAME SHAPE yaw\n");return 2;}
    memory=malloc(64u<<20);FILE*f=fopen(argv[1],"rb");if(!memory||!f)return 2;
    if(fread(memory,1,64u<<20,f)!=(64u<<20))return 2;fclose(f);
    uint8_t pal[768];f=fopen(argv[2],"rb");if(!f||fread(pal,1,768,f)!=768)return 2;fclose(f);
    for(int i=0;i<256;i++)palette[i]=0xff000000u|(pal[i*3]*255/63)|((pal[i*3+1]*255/63)<<8)|((pal[i*3+2]*255/63)<<16);
    tp_map_w=tp_map_ext=(uint32_t)atoi(argv[4]);
    int sweep=!strcmp(argv[5],"sweep"),gallery=!strcmp(argv[5],"frame")||sweep;double x,y,yaw;
    if(gallery){
        if(!sweep&&argc<9)return 2;M=memory;
        uint32_t map=r32(MAP_PTR),things=r32(THINGS);
        memset(memory+map,0,tp_map_w*tp_map_ext*4);
        memset(memory+things,0,1200*200);
        uint16_t idx=1,frame=sweep?1:(uint16_t)atoi(argv[6]);memcpy(memory+map+(32*tp_map_w+32)*4,&idx,2);
        uint8_t*t=memory+things+200;t[4]=0x18;t[7]=t[9]=32;memcpy(t+10,&frame,2);
        memory[0x1d3afc]=1;x=y=32;yaw=sweep?ART_YAW:atof(argv[8])*M_PI/180;
    }else{x=atof(argv[5]);y=atof(argv[6]);yaw=atof(argv[7])*M_PI/180;}
    if(sweep){
        int limit=atoi(argv[6]),nonempty=0,empty=0,drawn=0;uint32_t checksum=0;
        if(limit<1||limit>65536)return 2;
        uint32_t things=r32(THINGS);uint32_t image[160*100];double started=now();
        for(int frame=1;frame<limit;frame++){
            uint16_t fno=(uint16_t)frame;memcpy(memory+things+200+10,&fno,2);
            if(!tp_walk_start(32,32))return 3;tp_walk_frame();
            Snap*s=&snaps[front];if(!s->ncards){empty++;continue;}nonempty++;
            Card*c=&s->cards[0];float height=c->h*s->g.aspect/s->g.tws;
            float dist=fmaxf(c->w/s->g.tws,height)*1.7f+1;
            for(int side=0;side<3;side++){
                double a=ART_YAW+side*M_PI*.5;
                W.cam_x=c->x-cos(a)*dist;W.cam_y=c->y-sin(a)*dist;W.cam_yaw=a;
                W.cam_z=height*.55;W.cam_pitch=.07;
                if(!tp_walk_render(image,160,100))return 3;
                checksum=(uint32_t)crc32(checksum,(const Bytef*)image,sizeof image);drawn++;
            }
            if(frame%500==0){fprintf(stderr,"sweep: %d/%d frames, %.1fs\n",frame,limit,now()-started);fflush(stderr);}
        }
        FILE*out=fopen(argv[3],"w");if(!out)return 5;
        fprintf(out,"{\"frame_records_checked\":%d,\"nonempty\":%d,\"empty\":%d,\"views_rendered\":%d,\"crc32\":\"%08x\",\"seconds\":%.2f}\n",limit-1,nonempty,empty,drawn,checksum,now()-started);fclose(out);
        fprintf(stderr,"sweep complete: %d nonempty frames, %d views, %.1fs\n",nonempty,drawn,now()-started);free(memory);return 0;
    }
    if(!gallery&&argc>8)setenv("TP_WALK_MESH","0",1);
    if(!tp_walk_start(x,y))return 3;tp_walk_frame();
    if(gallery && argc>9 && !strcmp(argv[9],"source")) {
        Snap*s=&snaps[front];if(!s->ncards)return 4;Card*c=&s->cards[0];
        int scale=6,sw=c->w*scale,sh=c->h*scale;
        uint32_t*p=malloc((size_t)sw*sh*4);if(!p)return 4;
        for(int y=0;y<sh;y++)for(int x=0;x<sw;x++) {
            uint16_t colour=s->pix[c->off+(y/scale)*c->w+x/scale];
            p[y*sw+x]=colour<256 ? palette[colour]:0xff403830;
        }
        fprintf(stderr,"source %dx%d anchor %d,%d\n",c->w,c->h,c->ax,c->ay);
        int ok=png(argv[3],p,sw,sh);free(p);free(memory);return ok?0:5;
    }
    if(gallery){
        Snap*s=&snaps[front];if(!s->ncards){fprintf(stderr,"empty frame %s\n",argv[6]);return 4;}
        Card*c=&s->cards[0];c->shape=(uint8_t)atoi(argv[7]);
        if(argc>11 && !strcmp(argv[9],"model")) {
            c->cls=(uint8_t)atoi(argv[10]);c->type=(int16_t)atoi(argv[11]);
            const WMModel*m=wm_model_find(c->cls,c->type);
            if(!m || m->tw!=c->w || m->th!=c->h) {
                fprintf(stderr,"Requested model %d/%d missing, invalid, or incompatible with %dx%d source\n",c->cls,c->type,c->w,c->h);
                return 4;
            }
            fprintf(stderr,"Authored model %d/%d: %d triangles\n",c->cls,c->type,m->nt);
        }
        float width=c->w/s->g.tws,height=c->h*s->g.aspect/s->g.tws;
        double dist=fmax(width,height)*1.45+1;
        x=c->x-cos(yaw)*dist;y=c->y-sin(yaw)*dist;
        W.cam_x=x;W.cam_y=y;W.cam_yaw=yaw;W.cam_z=height*.55;W.cam_pitch=.07;
        if(getenv("TP_SNAPSHOT_EYE"))W.cam_z=atof(getenv("TP_SNAPSHOT_EYE"));
        if(getenv("TP_SNAPSHOT_PITCH"))W.cam_pitch=atof(getenv("TP_SNAPSHOT_PITCH"));
    }else{
        if(getenv("TP_MODEL_INSPECT")) {
            Snap*s=&snaps[front];for(int i=0;i<s->ncards;i++) {
                Card*c=&s->cards[i];const WMModel*m=wm_model_find(c->cls,c->type);
                if(m) fprintf(stderr,"Model %d/%d thing %d: %.2f,%.2f source %dx%d model %dx%d %s\n",
                    c->cls,c->type,c->idx,c->x,c->y,c->w,c->h,m->tw,m->th,
                    c->w==m->tw && c->h==m->th ? "compatible" : "sprite fallback");
            }
        }
        tp_walk_teleport(x,y,yaw);tp_walk_input(0,0,0,0,0,0,0);
    }
    int w=960,h=600;uint32_t*p=malloc((size_t)w*h*4);
    double began=now();if(!tp_walk_render(p,w,h))return 3;
    fprintf(stderr,"%d objects, %u source pixels, %.2f ms -> %s\n",snaps[front].ncards,snaps[front].npix,(now()-began)*1000,argv[3]);
    int ok=png(argv[3],p,w,h);free(p);free(memory);return ok?0:5;
}
