/* Replay captured native actor records through exact Reimagined pose bindings.
 * The camera follows the actor at a constant distance; this checks animation
 * selection and drawing, not live pacing or camera travel. Both projections
 * share geometry. Owned inputs and resulting pictures stay in ignored work/. */
#define main visitor_snapshot_main
#include "visitor_snapshot.c"
#undef main

int main(int argc,char**argv){
    if(argc!=9){
        fprintf(stderr,"usage: remaster_actor_replay BASE PALETTE CAPTUREDIR OUT THING COUNT YAW PROFILE\n");
        return 2;
    }
    char*end=NULL;long thing=strtol(argv[5],&end,10);
    if(!argv[5][0]||*end||thing<1||thing>=12000)return 2;
    long count=strtol(argv[6],&end,10);
    if(!argv[6][0]||*end||count<1||count>1000)return 2;
    double yaw=strtod(argv[7],&end)*M_PI/180;
    if(!argv[7][0]||*end||!isfinite(yaw))return 2;
    int result=2;FILE*f=NULL,*log=NULL;
    uint32_t*img=NULL;float*depth=NULL;
    memory=malloc(64u<<20);f=fopen(argv[1],"rb");
    if(!memory||!f||fread(memory,1,64u<<20,f)!=(64u<<20))goto done;
    fclose(f);f=NULL;M=memory;
    uint8_t pal[768];f=fopen(argv[2],"rb");
    if(!f||fread(pal,1,768,f)!=768)goto done;
    fclose(f);f=NULL;
    for(int i=0;i<256;i++)palette[i]=0xff000000u|(pal[i*3]*255/63)|
        ((pal[i*3+1]*255/63)<<8)|((pal[i*3+2]*255/63)<<16);
    setenv("TP_DATA",argv[8],1);setenv("TP_ART_MODE","reimagined",1);
    tp_map_w=tp_map_ext=128;
    uint32_t base=r32(THINGS),last=r32(THING_LAST),map=r32(MAP_PTR);
    if(last<base||(uint64_t)last+200>(64u<<20)||
       (uint64_t)base+(uint64_t)thing*200>last||
       !map||(uint64_t)map+128*128*4>(64u<<20))goto done;
    uint32_t bytes=last-base+200;
    char path[4096];snprintf(path,sizeof path,"%s/evidence.csv",argv[4]);
    log=fopen(path,"w");if(!log)goto done;
    fprintf(log,"sample,thing,class,variant,state,direction,frame,entity_x,entity_y,origin_x,origin_y,origin_z,triangles\n");
    img=malloc(960*600*sizeof*img);depth=malloc(640*640*sizeof*depth);
    if(!img||!depth)goto done;
    for(int sample=0;sample<count;sample++){
        result=3;
        snprintf(path,sizeof path,"%s/things-%03d.bin",argv[3],sample);
        f=fopen(path,"rb");
        if(!f||fread(memory+base,1,bytes,f)!=bytes||fgetc(f)!=EOF)goto done;
        fclose(f);f=NULL;
        uint32_t t=base+(uint32_t)thing*200;
        if(M[t+4]!=2&&M[t+4]!=8)goto done;
        if(M[t+7]>=128||M[t+9]>=128)goto done;
        double x=M[t+7]+M[t+6]/256.0,y=M[t+9]+M[t+8]/256.0;
        /* Only thing records were sampled, not changing map cell heads.
         * Isolate the actor in its recorded cell; retaining the base map's
         * old linked-list heads would place moving actors in stale cells.
         * Keep terrain, actor state, exact frame, overlays and fractional
         * coordinates unchanged. This is not a complete park replay. */
        for(int cell=0;cell<128*128;cell++)
            memory[map+cell*4]=memory[map+cell*4+1]=0;
        uint32_t cell=map+((uint32_t)M[t+9]*128+M[t+7])*4;
        memory[cell]=(uint8_t)thing;memory[cell+1]=(uint8_t)(thing>>8);
        memory[t+2]=memory[t+3]=0;
        if(!tp_walk_start(x,y))goto done;
        tp_walk_frame();Snap*s=&snaps[front];Card*c=NULL;
        for(int i=0;i<s->ncards;i++)if(s->cards[i].idx==thing){c=&s->cards[i];break;}
        result=4;
        const RMRig*rig=c&&c->rig_ok?rm_find(c->cls,c->type):NULL;
        const RMPose*pose=rig?rm_pose(rig,c->frame):NULL;
        if(!pose||rig->ns||c->frame!=(uint16_t)r16(t+10)||c->type!=M[t+0x59]){
            fprintf(stderr,"sample %d: unsupported native actor binding, frame %u eligible %u\n",
                    sample,c?c->frame:0,c?c->rig_ok:0);goto done;
        }
        /* Grounded staff body origins differ only by native pixel rounding. */
        if(c->cls==8&&(fabs(c->rig_x-x)>.05||fabs(c->rig_y-y)>.05)){
            fprintf(stderr,"sample %d: actor cell/origin mismatch %.6f %.6f versus %.6f %.6f\n",
                    sample,c->rig_x,c->rig_y,x,y);goto done;
        }
        Card captured=*c;s->cards[0]=captured;s->ncards=1;c=&s->cards[0];
        W.cam_x=c->rig_x-cos(yaw)*4;W.cam_y=c->rig_y-sin(yaw)*4;
        W.cam_yaw=yaw;W.cam_z=1.1;W.cam_pitch=.10;
        result=5;
        if(!tp_walk_render(img,960,600))goto done;
        snprintf(path,sizeof path,"%s/visitor-%03d.png",argv[4],sample);
        if(!png(path,img,960,600))goto done;
        for(int i=0;i<640*640;i++){img[i]=0xff332b24;depth[i]=FLT_MAX;}
        WMRender wr={.pixels=img,.depth=depth,.w=640,.h=640,
            .rx=1,.ry=1.f/3,.uy=-2.f/3,.uz=1/1.2f,.fy=-1,.fz=-1,
            .focal=280,.orthographic=1,.offset_y=140};
        rm_draw(&wr,rig,pose,0,0,1,NULL);
        snprintf(path,sizeof path,"%s/normal-%03d.png",argv[4],sample);
        if(!png(path,img,640,640))goto done;
        fprintf(log,"%d,%ld,%u,%u,%u,%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%u\n",
                sample,thing,c->cls,c->type,M[t+12],M[t+0x12],c->frame,
                x,y,c->rig_x,c->rig_y,c->rig_z,wr.triangles);
    }
    result=ferror(log)?6:0;
    if(!result)printf("%ld captured native actor states replayed in both projections\n",count);
done:
    if(f)fclose(f);
    if(log&&fclose(log)&&!result)result=6;
    rm_reset();free(img);free(depth);free(memory);memory=NULL;
    return result;
}
