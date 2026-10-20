/* Replay captured native vehicle/guest records through Reimagined bindings.
 * Both projections share the same meshes, exact poses and attachment matrices.
 * This is offline evidence, not a live window or frame-pacing test.
 * Inputs and outputs containing owned-game material stay under ignored work/. */
#define main visitor_snapshot_main
#include "visitor_snapshot.c"
#undef main

int main(int argc,char**argv){
    if(argc!=10){
        fprintf(stderr,"usage: remaster_vehicle_replay BASE PALETTE CAPTUREDIR OUT THING COUNT YAW PROFILE MAPWIDTH\n");
        return 2;
    }
    int result=2,thing=0,count=0,mapwidth=0;char *end=NULL;
    if(!bounded_arg(argv[5],11999,&thing)||!thing||
       !bounded_arg(argv[6],1000,&count)||!count||
       !bounded_arg(argv[9],128,&mapwidth)||(mapwidth!=64&&mapwidth!=128))return 2;
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
    uint32_t base=r32(THINGS),last=r32(THING_LAST),map=r32(MAP_PTR);
    if(last<base||(uint64_t)last+200>(64u<<20)||(uint64_t)base+thing*200>last||
       !map||(uint64_t)map+mapwidth*mapwidth*4>(64u<<20))goto done;
    uint32_t bytes=last-base+200;
    char path[4096];snprintf(path,sizeof path,"%s/evidence.csv",argv[4]);
    log=fopen(path,"w");if(!log)goto done;
    fprintf(log,"sample,frame,state,speed,linked_guests,attached_guests,visible_guests,variant_mask,origin_x,origin_y,origin_z,entity_x,entity_y,triangles,track_family,track_shape,bounce,contact_x,contact_y,contact_z\n");
    img=malloc(960*600*sizeof*img);depth=malloc(640*640*sizeof*depth);
    if(!img||!depth)goto done;
    for(int sample=0;sample<count;sample++){
        result=3;
        snprintf(path,sizeof path,"%s/things-%03d.bin",argv[3],sample);
        f=fopen(path,"rb");
        if(!f||fread(memory+base,1,bytes,f)!=bytes||fgetc(f)!=EOF)goto done;
        fclose(f);f=NULL;
        uint32_t t=base+thing*200;
        if(M[t+4]!=14||M[t+7]>=mapwidth||M[t+9]>=mapwidth)goto done;
        double x=M[t+7]+M[t+6]/256.0,y=M[t+9]+M[t+8]/256.0;
        /* Sampled thing records do not include changing cell heads. Rebuild
         * an isolated vehicle and its real track records in their recorded
         * cells. Keep guest links, terrain, slopes and source frames intact.
         * This checks recorded poses, not whole-park draw ordering or timing. */
        for(int cell=0;cell<mapwidth*mapwidth;cell++)memory[map+cell*4]=memory[map+cell*4+1]=0;
        for(unsigned idx=1;idx<bytes/200;idx++){
            uint32_t q=base+idx*200;
            if(M[q+4]!=18&&idx!=(unsigned)thing)continue;
            if(M[q+7]>=mapwidth||M[q+9]>=mapwidth)goto done;
            uint32_t cell=map+(M[q+9]*mapwidth+M[q+7])*4;
            memory[q+2]=memory[cell];memory[q+3]=memory[cell+1];
            memory[cell]=(uint8_t)idx;memory[cell+1]=(uint8_t)(idx>>8);
        }
        /* Start once, as the actual Visitor session does. Reopening for every
         * sample would discard all loaded models and conceal cache behavior.
         * Force a fresh snapshot because offline samples can arrive sooner
         * than the live observer's 60 Hz throttle. */
        if(!sample){if(!tp_walk_start(x,y))goto done;}
        else {W.x=x;W.y=y;last_snap=0;}
        tp_walk_frame();Snap*s=&snaps[front];Card*c=NULL;
        for(int i=0;i<s->ncards;i++)if(s->cards[i].idx==thing){c=&s->cards[i];break;}
        result=4;
        const RMRig*ride=c&&c->rig_ok?rm_find_frame(c->cls,c->frame):NULL;
        const RMPose*pose=ride?rm_pose(ride,c->frame):NULL;
        if(!pose||!tp_remaster_riders_valid(ride->ns,c->seats,c->rig_riders)){
            fprintf(stderr,"sample %d: unsupported native ride binding, frame %u eligible %u\n",
                    sample,c?c->frame:0,c?c->rig_ok:0);goto done;
        }
        const RMRig*guests[40]={0};const RMPose*poses[40]={0};
        int attached=0,visible=0,linked=0;unsigned variants=0;
        for(int seat=0;seat<40;seat++)if(c->seats[seat]!=255){
            attached++;variants|=1u<<c->seats[seat];
            if(!c->rig_riders[seat].visible)continue;
            guests[seat]=rm_find(2,c->seats[seat]);
            poses[seat]=guests[seat]?rm_pose(guests[seat],c->rig_riders[seat].pose):NULL;
            if(!poses[seat]){
                fprintf(stderr,"sample %d seat %d: missing identity %u pose %u\n",
                        sample,seat,c->seats[seat],c->rig_riders[seat].pose);goto done;
            }
            visible++;
        }
        for(unsigned next=(uint16_t)r16(t+0x47);next;){
            if(++linked>40||(uint64_t)base+next*200>last)goto done;
            next=(uint16_t)r16(base+next*200+0x47);
        }
        float placement[12],posed_contact[3],contact[3];
        if(!rm_vehicle_transform(&c->vehicle,ride,pose,placement)){
            fprintf(stderr,"sample %d: missing or invalid authored vehicle path\n",sample);goto done;
        }
        const RMMotionBody*body=rm_motion_body((unsigned)ride->cls,(unsigned)ride->type);
        rm_point(pose->matrices+body->bone*12,body->pivot[0],body->pivot[1],body->pivot[2],posed_contact);
        rm_point(placement,posed_contact[0],posed_contact[1],posed_contact[2],contact);
        float contact_x=c->vehicle.track_x+1.f/3+contact[0],contact_y=c->vehicle.track_y+.5f+contact[1];
        Card captured=*c;s->cards[0]=captured;s->ncards=1;c=&s->cards[0];
        W.cam_x=contact_x+.4-cos(yaw)*6;W.cam_y=contact_y-1-sin(yaw)*6;
        W.cam_yaw=yaw;W.cam_z=.9+contact[2];W.cam_pitch=.07;
        result=5;
        if(!tp_walk_render(img,960,600))goto done;
        snprintf(path,sizeof path,"%s/visitor-%03d.png",argv[4],sample);
        if(!png(path,img,960,600))goto done;
        for(int i=0;i<640*640;i++){img[i]=0xff332b24;depth[i]=FLT_MAX;}
        WMRender wr={.pixels=img,.depth=depth,.w=640,.h=640,
            .rx=1,.ry=1.f/3,.uy=-2.f/3,.uz=1/1.2f,.fy=-1,.fz=-1,
            .cx=contact[0],.cy=contact[1],.cz=contact[2],
            .focal=168,.orthographic=1,.offset_x=-40,.offset_y=200};
        rm_draw(&wr,ride,pose,0,0,1,placement);
        for(int seat=0;seat<ride->ns;seat++)if(guests[seat]){
            float attachment[12];rm_affine_product(attachment,placement,pose->matrices+(ride->nb+seat)*12);
            rm_draw(&wr,guests[seat],poses[seat],0,0,1,attachment);
        }
        snprintf(path,sizeof path,"%s/normal-%03d.png",argv[4],sample);
        if(!png(path,img,640,640))goto done;
        fprintf(log,"%d,%u,%u,%u,%d,%d,%d,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%u,%u,%.6f,%.6f,%.6f\n",
                sample,c->frame,M[t+12],(uint16_t)r16(t+0x2e),
                linked,attached,visible,variants,c->rig_x,c->rig_y,c->rig_z,x,y,wr.triangles,c->vehicle.family,c->vehicle.shape,c->vehicle.bounce,contact_x,contact_y,contact[2]);
    }
    result=ferror(log)?6:0;
    if(!result)printf("%d captured native states replayed in both projections\n",count);
done:
    if(f)fclose(f);
    if(log&&fclose(log)&&!result)result=6;
    rm_reset();free(img);free(depth);free(memory);memory=NULL;
    return result;
}
