/* Bake the same model/pose used in Visitor View into the native park projection.
 * Output: TPBAKE01, little-endian u32 width,height, then straight RGBA pixels.
 * This is an authoring tool; not an animation timer or a complete asset pack. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../runtime/walk_mesh.h"
#include "../runtime/walk_rig.h"
static void u32(FILE*f,unsigned v) {
    uint8_t p[4]={v,v>>8,v>>16,v>>24};fwrite(p,1,4,f);
}
int main(int argc,char**argv) {
    if(argc!=10 && argc!=11){
        fprintf(stderr,"usage: remaster_bake MODEL FRAME OUT WIDTH HEIGHT PIXELS_PER_CELL ANCHOR_X ANCHOR_Y ASPECT [BONE]\n");
        return 2;
    }
    int frame=atoi(argv[2]),w=atoi(argv[4]),h=atoi(argv[5]);
    float scale=strtof(argv[6],NULL),ax=strtof(argv[7],NULL),ay=strtof(argv[8],NULL),aspect=strtof(argv[9],NULL);
    if(frame<0||frame>65535||w<1||h<1||w>2048||h>2048||!isfinite(scale)||scale<1||scale>1024||
       !isfinite(ax)||!isfinite(ay)||fabsf(ax)>4096||fabsf(ay)>4096||!isfinite(aspect)||aspect<.1||aspect>10)return 2;
    FILE*f=fopen(argv[1],"r");if(!f)return 3;
    RMRig model={0},*rig=&model;int ok=rm_read(f,rig);fclose(f);
    if(!ok){rm_free(rig);return 3;}
    if(argc==11){
        char*end=NULL;long bone=strtol(argv[10],&end,10);
        if(!argv[10][0] || *end || bone<0 || bone>=rig->nb){rm_free(rig);return 2;}
        /* Multi-part native sprites can share a canonical mesh. Export only
         * this authored group while retaining the full model's origin/pose. */
        int n=0;
        for(int i=0;i<rig->nt;i++){
            RMTriangle t=rig->t[i];
            if(rig->v[t.a].bone==(unsigned)bone && rig->v[t.b].bone==(unsigned)bone &&
               rig->v[t.c].bone==(unsigned)bone)rig->t[n++]=t;
        }
        rig->nt=n;if(!n){rm_free(rig);return 4;}
    }
    const RMPose*p=rm_pose(rig,(unsigned)frame);if(!p){rm_free(rig);return 4;}
    uint32_t*pixels=calloc((size_t)w*h,4);float*depth=malloc((size_t)w*h*sizeof*depth);
    if(!pixels||!depth){free(pixels);free(depth);rm_free(rig);return 5;}
    for(int i=0;i<w*h;i++)depth[i]=FLT_MAX;
    /* Native park shear: x=24X+8Y, y=16Y-20Z at low resolution.
     * Positive Y points toward the observer; shared geometry resolves overlaps. */
    WMRender r={.pixels=pixels,.depth=depth,.w=w,.h=h,.rx=1,.ry=1.f/3,
        .ux=0,.uy=-2.f/3,.uz=1/aspect,.fx=0,.fy=-1,.fz=-1,
        .focal=scale,.orthographic=1,.offset_x=ax-w*.5f,.offset_y=ay-h*.5f};
    rm_draw(&r,rig,p,0,0,1,NULL);
    f=fopen(argv[3],"wb");if(!f){free(pixels);free(depth);rm_free(rig);return 6;}
    fwrite("TPBAKE01",1,8,f);u32(f,w);u32(f,h);
    for(int i=0;i<w*h;i++)u32(f,pixels[i]);
    int failed=ferror(f);if(fclose(f))failed=1;
    printf("%u triangles, %u fragments; source pose %d, %dx%d\n",r.triangles,r.fragments,frame,w,h);
    free(pixels);free(depth);rm_free(rig);return failed?6:0;
}
