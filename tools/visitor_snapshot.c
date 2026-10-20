/* Offline visual inspection of a copied TP_DUMP memory snapshot. No game runs,
 * no saves are written. Include the private walk implementation to inspect its
 * snapshots and construct an isolated frame gallery without a public game API.
 * cc -O2 -I runtime tools/visitor_snapshot.c runtime/native_land.c
 *    runtime/native_draw.c -lz -lm -lpthread -o work/visitor-3d/native/snapshot
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth x y yaw [original]
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth frame FRAME SHAPE yaw
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth shop FRAME TYPE yaw [customer ID]
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth guest FRAME VARIANT yaw [state SUBTYPE]
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth staff FRAME VARIANT yaw [state SUBTYPE]
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth ride FRAME TYPE yaw [guests IDENTITIES]
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth track FRAME FAMILY yaw segment SHAPE LEVEL
 * snapshot memory.bin MPALETTE.DAT out.png mapwidth car FAMILY SHAPE yaw motion DIR LEVEL X Y [guests IDENTITIES]
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
static void chunk(FILE*f,const char*t,const uint8_t*p,size_t n){be32(f,(uint32_t)n);fwrite(t,1,4,f);if(n)fwrite(p,1,n,f);uint32_t c=(uint32_t)crc32(0,(const Bytef*)t,4);if(n)c=(uint32_t)crc32(c,p,(uInt)n);be32(f,c);}
static int png(const char*path,uint32_t*p,int w,int h){
    size_t stride=1+(size_t)w*3,n=stride*h;uint8_t*raw=malloc(n);if(!raw)return 0;
    for(int y=0;y<h;y++){raw[y*stride]=0;for(int x=0;x<w;x++){uint32_t c=p[y*w+x];size_t i=y*stride+1+x*3;raw[i]=c;raw[i+1]=c>>8;raw[i+2]=c>>16;}}
    uLongf zn=compressBound(n);uint8_t*z=malloc(zn);if(!z){free(raw);return 0;}compress2(z,&zn,raw,n,6);
    FILE*f=fopen(path,"wb");if(!f){free(z);free(raw);return 0;}fwrite("\x89PNG\r\n\x1a\n",1,8,f);
    uint8_t head[13]={w>>24,w>>16,w>>8,w,h>>24,h>>16,h>>8,h,8,2,0,0,0};chunk(f,"IHDR",head,13);chunk(f,"IDAT",z,zn);chunk(f,"IEND",NULL,0);fclose(f);free(z);free(raw);return 1;
}
static int bounded_arg(const char *s,int maximum,int *out){
    char *end=NULL;long n=strtol(s,&end,10);
    if(!*s || *end || n<0 || n>maximum)return 0;*out=(int)n;return 1;
}
int main(int argc,char**argv){
    if(argc<8){fprintf(stderr,"usage: snapshot memory palette output mapwidth x y yaw [original] | ... frame FRAME SHAPE yaw\n");return 2;}
    memory=malloc(64u<<20);FILE*f=fopen(argv[1],"rb");if(!memory||!f)return 2;
    if(fread(memory,1,64u<<20,f)!=(64u<<20))return 2;fclose(f);
    uint8_t pal[768];f=fopen(argv[2],"rb");if(!f||fread(pal,1,768,f)!=768)return 2;fclose(f);
    for(int i=0;i<256;i++)palette[i]=0xff000000u|(pal[i*3]*255/63)|((pal[i*3+1]*255/63)<<8)|((pal[i*3+2]*255/63)<<16);
    tp_map_w=tp_map_ext=(uint32_t)atoi(argv[4]);
    int shop=!strcmp(argv[5],"shop");
    int guest=!strcmp(argv[5],"guest");
    int staff=!strcmp(argv[5],"staff");
    int ride=!strcmp(argv[5],"ride");
    int track=!strcmp(argv[5],"track");
    int car=!strcmp(argv[5],"car");
    int sweep=!strcmp(argv[5],"sweep"),gallery=!strcmp(argv[5],"frame")||sweep||shop||guest||staff||ride||track||car;double x,y,yaw;
    if(gallery){
        if(!sweep&&argc<9)return 2;M=memory;
        uint32_t map=r32(MAP_PTR),things=r32(THINGS);
        memset(memory+map,0,tp_map_w*tp_map_ext*4);
        memset(memory+things,0,1200*200);
        uint16_t idx=1,frame=sweep?1:(track||car)?0:(uint16_t)atoi(argv[6]);memcpy(memory+map+(32*tp_map_w+32)*4,&idx,2);
        uint8_t*t=memory+things+200;t[4]=0x18;t[7]=t[9]=32;memcpy(t+10,&frame,2);
        if(track){
            int family,shape,level,source_frame;
            if(argc!=12 || strcmp(argv[9],"segment") ||
               !bounded_arg(argv[6],65535,&source_frame) ||
               !bounded_arg(argv[7],3,&family) || family<1 ||
               !bounded_arg(argv[10],family==1?20:family==2?22:10,&shape) ||
               !bounded_arg(argv[11],255,&level))return 2;
            frame=(uint16_t)source_frame;memcpy(t+10,&frame,2);
            t[4]=0x12;t[12]=2;t[0x16]=(uint8_t)family;t[0x44]=(uint8_t)shape;t[0x79]=(uint8_t)level;
        }
        if(car){
            int family,shape,direction,level,px,py;
            if((argc!=14 && argc!=16) || strcmp(argv[9],"motion") ||
               !bounded_arg(argv[6],3,&family) || family<1 ||
               !bounded_arg(argv[7],family==1?20:family==2?22:10,&shape) ||
               !bounded_arg(argv[10],3,&direction) || !bounded_arg(argv[11],255,&level) ||
               !bounded_arg(argv[12],255,&px) || !bounded_arg(argv[13],255,&py))return 2;
            uint16_t next=2;memcpy(t+2,&next,2);t[4]=14;t[12]=family==3?0x22:8;
            t[6]=(uint8_t)px;t[8]=(uint8_t)py;t[0x12]=(uint8_t)direction;
            uint8_t *track_record=memory+things+400;
            track_record[4]=18;track_record[7]=track_record[9]=32;track_record[12]=2;
            track_record[0x16]=(uint8_t)family;track_record[0x44]=(uint8_t)shape;track_record[0x79]=(uint8_t)level;
            if(argc==16){
                if(strcmp(argv[14],"guests"))return 2;size_t n=strlen(argv[15]);if(n>40)return 2;
                for(size_t i=0;i<n;i++){
                    if(argv[15][i]<'0' || argv[15][i]>'7')return 2;
                    next=(uint16_t)(i+3);uint8_t *prev=i?memory+things+(i+2)*200:t;
                    memcpy(prev+0x47,&next,2);uint8_t *guest_record=memory+things+next*200;
                    guest_record[4]=2;guest_record[0x59]=(uint8_t)(argv[15][i]-'0');
                }
            }
        }
        if(shop){
            int type=atoi(argv[7]);if(type<0||type>=17)return 2;
            uint16_t record=(uint16_t)(type*107);
            t[4]=0x0a;t[12]=2;t[6]=t[8]=128;memcpy(t+0x16,&record,2);
            /* Isolate the requested state in this private memory copy. */
            memory[0x1d3c62+record+0x67]=255;
            if(argc>9 && !strcmp(argv[9],"customer")){
                if(argc!=11)return 2;
                char*end=NULL;long variant=strtol(argv[10],&end,10);
                if(!argv[10][0] || *end || variant<0 || variant>=8)return 2;
                uint16_t customer=2;memcpy(t+0x47,&customer,2);
                uint8_t*p=memory+things+400;p[4]=2;p[0x59]=(uint8_t)variant;
            }
        }
        if(guest||staff){
            int variant=atoi(argv[7]);if(variant<0||variant>=(staff?9:8))return 2;
            t[4]=staff?8:2;t[12]=staff?memory[0x1d35ec+variant*54]:8;
            t[6]=t[8]=128;t[0x59]=(uint8_t)variant;
            if(staff)t[0x9c]=t[0x9d]=255; /* isolate body from job marker */
            if(argc>10 && !strcmp(argv[9],"state")){
                char*end=NULL;long state=strtol(argv[10],&end,0);
                if(!argv[10][0] || *end || state<0 || state>255)return 2;
                t[12]=(uint8_t)state;
            }
            memset(memory+0x1d6678,0,2); /* no selection-arrow fixture */
        }
        if(ride){
            int type=atoi(argv[7]);if(type<0||type>=29)return 2;
            uint16_t record=(uint16_t)(type*200);
            t[4]=4;t[12]=2;t[6]=t[8]=128;memcpy(t+0x16,&record,2);
            t[0x32]=1; /* occupied bouncy-castle dispatch, native kind 9 */
            if(argc>10 && !strcmp(argv[9],"guests")){
                size_t n=strlen(argv[10]);
                int cap=r16(0x1cced0+record+0xa6);
                if(n>36 || n>(unsigned)cap)return 2;
                for(size_t i=0;i<n;i++){
                    if(argv[10][i]<'0'||argv[10][i]>'7')return 2;
                    uint16_t next=(uint16_t)(i+2);
                    uint8_t*prev=i?memory+things+(i+1)*200:t;
                    memcpy(prev+0x47,&next,2);
                    uint8_t*p=memory+things+(i+2)*200;
                    p[4]=2;p[0x59]=(uint8_t)(argv[10][i]-'0');
                }
            }
        }
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
    if(car){
        /* The parent record is necessary for native selection. The car
         * gallery isolates its captured card, without changing that selection. */
        Snap *s=&snaps[front];int found=-1;
        for(int i=0;i<s->ncards;i++)if(s->cards[i].cls==14){found=i;break;}
        if(found<0)return 4;s->cards[0]=s->cards[found];s->ncards=1;
    }
    if(gallery && getenv("TP_SNAPSHOT_CAPTURE_DIGEST")){
        Snap*s=&snaps[front];if(s->ncards!=1)return 4;Card*c=&s->cards[0];
        uint32_t crc=(uint32_t)crc32(0,(const Bytef*)(s->pix+c->off),(size_t)c->w*c->h*2);
        FILE*out=fopen(argv[3],"w");if(!out)return 5;
        fprintf(out,"{\"width\":%u,\"height\":%u,\"anchor\":[%d,%d],\"source_crc32\":\"%08x\",\"eligible\":%u,\"native_frame\":%u,\"native_origin\":[%.7g,%.7g,%.7g],\"attachments\":[",
                c->w,c->h,c->ax,c->ay,crc,c->rig_ok,c->frame,c->rig_x,c->rig_y,c->rig_z);
        int n=0;for(int i=0;i<40;i++)if(c->rig_riders[i].present){
            const TPRMRider*p=&c->rig_riders[i];
            fprintf(out,"%s{\"slot\":%d,\"identity\":%u,\"pose\":%u,\"visible\":%u}",
                    n++?",":"",i,p->variant,p->pose,p->visible);
        }
        fputs("],\"components\":[",out);
        for(unsigned i=0;i<c->ncomponents;i++){
            const TPRMPlacedComponent *p=&s->components[c->component_first+i];
            fprintf(out,"%s{\"frame\":%u,\"origin\":[%.7g,%.7g,%.7g]}",i?",":"",p->frame,p->x,p->y,p->z);
        }
        fputs("]}\n",out);int failed=ferror(out);if(fclose(out))failed=1;
        free(memory);return failed?5:0;
    }
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
        Card*c=&s->cards[0];if(!shop && !guest && !staff && !ride && !track && !car)c->shape=(uint8_t)atoi(argv[7]);
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
        /* A guest's exaggerated reaction bounds must not move the camera.
         * Use one fixed composition for all identities, keys and subtypes. */
        if(guest||staff){
            dist=staff?4:3.2;height=staff?1.8:1.25;
            fprintf(stderr,"%s %d state %u source frame %u: native origin %.6f %.6f lift %.6f eligible %u\n",
                    staff?"staff":"guest",c->type,memory[r32(THINGS)+200+12],c->frame,
                    c->rig_x,c->rig_y,c->rig_z,c->rig_ok);
        }
        x=c->x-cos(yaw)*dist;y=c->y-sin(yaw)*dist;
        if(car){
            dist=4;height=1.6;
            x=c->rig_x-cos(yaw)*dist;y=c->rig_y-sin(yaw)*dist;
            fprintf(stderr,"car frame %u: native origin %.6f %.6f lift %.6f eligible %u\n",c->frame,c->rig_x,c->rig_y,c->rig_z,c->rig_ok);
        }
        if(track && c->ncomponents){
            const TPRMPlacedComponent *p=&s->components[c->component_first];
            x=p->x-cos(yaw)*dist;y=p->y-sin(yaw)*dist;
            fprintf(stderr,"track family %s segment %s: %u native components\n",argv[7],argv[10],c->ncomponents);
        }
        if(ride){
            dist=6;height=2;
            x=c->rig_x+.4-cos(yaw)*dist;y=c->rig_y-1-sin(yaw)*dist;
            fprintf(stderr,"ride %d frame %u: native origin %.6f %.6f eligible %u\n",
                    c->type,c->frame,c->rig_x,c->rig_y,c->rig_ok);
            for(int i=0;i<40;i++)if(c->seats[i]!=255)
                fprintf(stderr,"seat %d identity %u pose %u present %u visible %u\n",
                        i,c->seats[i],c->rig_riders[i].pose,
                        c->rig_riders[i].present,c->rig_riders[i].visible);
        }
        if(shop){
            fprintf(stderr,"shop %d frame %u: native origin %.6f %.6f eligible %u\n",
                    c->type,c->frame,c->rig_x,c->rig_y,c->rig_ok);
            for(int i=0;i<40;i++)if(c->seats[i]!=255)
                fprintf(stderr,"customer part %d identity %u pose %u present %u visible %u\n",
                        i,c->seats[i],c->rig_riders[i].pose,
                        c->rig_riders[i].present,c->rig_riders[i].visible);
        }
        W.cam_x=x;W.cam_y=y;W.cam_yaw=yaw;W.cam_z=height*.55+(car?c->rig_z:0);W.cam_pitch=.07;
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
    if(getenv("TP_SNAPSHOT_EYE"))W.cam_z=atof(getenv("TP_SNAPSHOT_EYE"));
    if(getenv("TP_SNAPSHOT_PITCH"))W.cam_pitch=atof(getenv("TP_SNAPSHOT_PITCH"));
    if(gallery && getenv("TP_SNAPSHOT_ORTHOGRAPHIC")){
        Snap*s=&snaps[front];if(s->ncards!=1)return 4;Card*c=&s->cards[0];
        const RMRig*rig=c->rig_ok?((c->cls==14 || c->cls==24)?rm_find_frame(c->cls,c->frame):rm_find(c->cls,c->type)):NULL;
        const RMPose*pose=rig?rm_pose(rig,c->frame):NULL;
        if(!c->ncomponents && (!pose || !tp_remaster_riders_valid(rig->ns,c->seats,c->rig_riders)))return 4;
        const RMRig*guests[40]={0};const RMPose*poses[40]={0};
        for(int i=0;i<40;i++)if(c->seats[i]!=255 && c->rig_riders[i].visible){
            guests[i]=rm_find(2,c->seats[i]);poses[i]=guests[i]?rm_pose(guests[i],c->rig_riders[i].pose):NULL;
            if(!poses[i])return 4;
        }
        uint32_t*p=malloc(640*640*4);float*depth=malloc(640*640*sizeof*depth);
        if(!p||!depth){free(p);free(depth);return 4;}
        for(int i=0;i<640*640;i++){p[i]=0xff332b24;depth[i]=FLT_MAX;}
        WMRender wr={.pixels=p,.depth=depth,.w=640,.h=640,
            .rx=1,.ry=1.f/3,.uy=-2.f/3,.uz=1/1.2f,.fy=-1,.fz=-1,
            .focal=160,.orthographic=1,.offset_y=170};
        if(c->ncomponents){
            if(c->component_first>=s->ncomponents){free(p);free(depth);return 4;}
            wr.cx=s->components[c->component_first].x;wr.cy=s->components[c->component_first].y;
            if(!draw_track_assembly(s,c,&wr,1)){free(p);free(depth);return 4;}
        }else{
            float placement[12]={1,0,0,0,0,1,0,0,0,0,1,c->rig_z};
            if(c->cls==14 && !rm_vehicle_transform(&c->vehicle,rig,pose,placement)){free(p);free(depth);return 4;}
            rm_draw(&wr,rig,pose,0,0,1,placement);
            for(int i=0;i<rig->ns;i++)if(guests[i]){
                float attachment[12];rm_affine_product(attachment,placement,pose->matrices+(rig->nb+i)*12);
                rm_draw(&wr,guests[i],poses[i],0,0,1,attachment);
            }
        }
        fprintf(stderr,"native composite orthographic: %u triangles, %u fragments\n",wr.triangles,wr.fragments);
        int ok=png(argv[3],p,640,640);free(p);free(depth);free(memory);return ok?0:5;
    }
    int w=960,h=600;uint32_t*p=malloc((size_t)w*h*4);
    double began=now();if(!tp_walk_render(p,w,h))return 3;
    fprintf(stderr,"%d objects, %u source pixels, %.2f ms -> %s\n",snaps[front].ncards,snaps[front].npix,(now()-began)*1000,argv[3]);
    int ok=png(argv[3],p,w,h);free(p);free(memory);return ok?0:5;
}
