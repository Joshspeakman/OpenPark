/* Replay captured native ride/guest records through Reimagined bindings.
 * Both projections share the same meshes, exact poses and attachment matrices.
 * This is offline evidence, not a live window or frame-pacing test.
 * Inputs and outputs containing owned-game material stay under ignored work/. */
#define main visitor_snapshot_main
#include "visitor_snapshot.c"
#undef main

int main(int argc,char**argv){
    if(argc!=9 && argc!=10){
        fprintf(stderr,"usage: remaster_ride_replay BASE PALETTE CAPTUREDIR OUT THING COUNT YAW PROFILE [MAPWIDTH]\n");
        return 2;
    }
    int result=2,thing=0,count=0,mapwidth=128;char*end=NULL;
    if(!bounded_arg(argv[5],11999,&thing)||!thing||!bounded_arg(argv[6],1000,&count)||!count||
       (argc==10&&(!bounded_arg(argv[9],128,&mapwidth)||(mapwidth!=64&&mapwidth!=128))))return 2;
    double yaw=strtod(argv[7],&end)*M_PI/180;
    if(!argv[7][0]||*end||!isfinite(yaw))return 2;
    FILE*f=NULL,*log=NULL;
    uint32_t*img=NULL;float*depth=NULL;
    if(thing<1||thing>=12000||count<1||count>1000||!isfinite(yaw))goto done;
    memory=malloc(64u<<20);f=fopen(argv[1],"rb");
    if(!memory||!f||fread(memory,1,64u<<20,f)!=(64u<<20))goto done;
    fclose(f);f=NULL;M=memory;
    uint8_t pal[768];f=fopen(argv[2],"rb");
    if(!f||fread(pal,1,768,f)!=768)goto done;
    fclose(f);f=NULL;
    for(int i=0;i<256;i++)palette[i]=0xff000000u|(pal[i*3]*255/63)|
        ((pal[i*3+1]*255/63)<<8)|((pal[i*3+2]*255/63)<<16);
    setenv("TP_DATA",argv[8],1);setenv("TP_ART_MODE","reimagined",1);
    tp_map_w=tp_map_ext=(unsigned)mapwidth;
    uint32_t base=r32(THINGS),last=r32(THING_LAST);
    if(last<base||(uint64_t)last+200>(64u<<20)||(uint64_t)base+thing*200>last)goto done;
    uint32_t bytes=last-base+200;
    char path[4096];snprintf(path,sizeof path,"%s/evidence.csv",argv[4]);
    log=fopen(path,"w");if(!log)goto done;
    fprintf(log,"sample,frame,running,speed,linked_guests,attached_guests,visible_guests,variant_mask,origin_x,origin_y,triangles,phase\n");
    img=malloc(960*600*sizeof*img);depth=malloc(640*640*sizeof*depth);
    if(!img||!depth)goto done;
    for(int sample=0;sample<count;sample++){
        result=3;
        snprintf(path,sizeof path,"%s/things-%03d.bin",argv[3],sample);
        f=fopen(path,"rb");
        if(!f||fread(memory+base,1,bytes,f)!=bytes||fgetc(f)!=EOF)goto done;
        fclose(f);f=NULL;
        uint32_t t=base+thing*200;
        double x=M[t+7]+M[t+6]/256.0,y=M[t+9]+M[t+8]/256.0;
        if(M[t+4]!=4||M[t+7]>=mapwidth||M[t+9]>=mapwidth)goto done;
        if(!sample){if(!tp_walk_start(x,y))goto done;}
        else{W.x=x;W.y=y;last_snap=0;}
        tp_walk_frame();Snap*s=&snaps[front];Card*c=NULL;
        for(int i=0;i<s->ncards;i++)if(s->cards[i].idx==thing){c=&s->cards[i];break;}
        result=4;
        const RMRig*ride=c&&c->rig_ok?rm_find(c->cls,c->type):NULL;
        const RMPose*pose=ride?rm_pose(ride,c->frame):NULL;
        if(!pose||!tp_remaster_riders_valid(ride->ns,c->seats,c->rig_riders)){
            fprintf(stderr,"sample %d: unsupported native ride binding, frame %u eligible %u\n",
                    sample,c?c->frame:0,c?c->rig_ok:0);goto done;
        }
        const RMRig*guests[40]={0};const RMPose*poses[40]={0};
        if(!rm_resolve_riders(ride,c->seats,c->rig_riders,guests,poses)){
            fprintf(stderr,"sample %d: missing exact occupied-rider pose\n",sample);goto done;
        }
        int attached=0,visible=0,linked=0;unsigned variants=0;
        for(int seat=0;seat<40;seat++)if(c->seats[seat]!=255){
            attached++;variants|=1u<<c->seats[seat];visible+=c->rig_riders[seat].visible;
        }
        for(unsigned next=(uint16_t)r16(t+0x47);next;){
            if(++linked>36||(uint64_t)base+next*200>last)goto done;
            next=(uint16_t)r16(base+next*200+0x47);
        }
        Card captured=*c;s->cards[0]=captured;s->ncards=1;c=&s->cards[0];
        W.cam_x=c->rig_x+.4-cos(yaw)*6;W.cam_y=c->rig_y-1-sin(yaw)*6;
        W.cam_yaw=yaw;W.cam_z=1.1;W.cam_pitch=.07;
        result=5;
        if(!tp_walk_render(img,960,600))goto done;
        snprintf(path,sizeof path,"%s/visitor-%03d.png",argv[4],sample);
        if(!png(path,img,960,600))goto done;
        for(int i=0;i<640*640;i++){img[i]=0xff332b24;depth[i]=FLT_MAX;}
        WMRender wr={.pixels=img,.depth=depth,.w=640,.h=640,
            .rx=1,.ry=1.f/3,.uy=-2.f/3,.uz=1/1.2f,.fy=-1,.fz=-1,
            .focal=168,.orthographic=1,.offset_x=-40,.offset_y=200};
        rm_draw(&wr,ride,pose,0,0,1,NULL);
        for(int seat=0;seat<ride->ns;seat++)if(guests[seat])
            rm_draw(&wr,guests[seat],poses[seat],0,0,1,pose->matrices+(ride->nb+seat)*12);
        snprintf(path,sizeof path,"%s/normal-%03d.png",argv[4],sample);
        if(!png(path,img,640,640))goto done;
        fprintf(log,"%d,%u,%u,%u,%d,%d,%d,%u,%.6f,%.6f,%u,%u\n",
                sample,c->frame,(uint16_t)r16(t+0x32),(uint16_t)r16(t+0x2e),
                linked,attached,visible,variants,c->rig_x,c->rig_y,wr.triangles,r32(t+0x20));
    }
    result=ferror(log)?6:0;
    if(!result)printf("%d captured native states replayed in both projections\n",count);
done:
    if(f)fclose(f);
    if(log&&fclose(log)&&!result)result=6;
    rm_reset();free(img);free(depth);free(memory);memory=NULL;
    return result;
}
