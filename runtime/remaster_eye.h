/* Explicit model-space eye anchors. TPREYES1 count, then sorted rows:
 * model_class model_type head_bone xyz forward_xyz. No inferred eye height. */
#ifndef TP_REMASTER_EYE_H
#define TP_REMASTER_EYE_H
typedef struct {unsigned cls,type,bone;float point[3],forward[3];} RMEye;
static RMEye rm_eyes[256];static unsigned rm_neye;static int rm_eyes_loaded;
static int rm_eyes_read(FILE*f,RMEye*out,unsigned*count){
    char magic[16],extra;unsigned n;*count=0;
    if(fscanf(f,"%15s%u",magic,&n)!=2||strcmp(magic,"TPREYES1")||!n||n>256)return 0;
    for(unsigned i=0;i<n;i++){
        RMEye*a=&out[i];
        if(fscanf(f,"%u%u%u",&a->cls,&a->type,&a->bone)!=3||a->cls>255||a->type>32767||a->bone>=64||
           (i&&(a->cls<out[i-1].cls||(a->cls==out[i-1].cls&&a->type<=out[i-1].type))))return 0;
        for(int k=0;k<3;k++)if(fscanf(f,"%f",a->point+k)!=1||!isfinite(a->point[k])||fabsf(a->point[k])>32)return 0;
        for(int k=0;k<3;k++)if(fscanf(f,"%f",a->forward+k)!=1||!isfinite(a->forward[k])||fabsf(a->forward[k])>32)return 0;
        if(!rm_unit3(a->forward))return 0;
    }
    if(fscanf(f," %c",&extra)==1)return 0;
    *count=n;return 1;
}
static const RMEye*rm_eye_find(unsigned cls,unsigned type){
    if(!rm_eyes_loaded){
        rm_eyes_loaded=1;const char*root=getenv("TP_DATA");char path[PATH_MAX];
        if(root){int n=snprintf(path,sizeof path,"%s/reimagined/eyes.map",root);
            if(n>0&&(size_t)n<sizeof path){FILE*f=fopen(path,"r");
                if(f){if(!rm_eyes_read(f,rm_eyes,&rm_neye))fprintf(stderr,"Reimagined: invalid eye anchors %s\n",path);fclose(f);}}}
    }
    for(unsigned i=0;i<rm_neye;i++)if(rm_eyes[i].cls==cls&&rm_eyes[i].type==type)return &rm_eyes[i];
    return NULL;
}
/* Compose the actual head pose with its vehicle/seat parent. Direction is a
 * vector: translations never affect it. Singular/hidden heads are ineligible. */
static int rm_eye_transform(const RMEye*a,const RMRig*r,const RMPose*p,const float*parent,float*position,float*forward){
    if(!a||!r||!p||a->cls!=(unsigned)r->cls||a->type!=(unsigned)r->type||a->bone>=(unsigned)r->nb)return 0;
    float composed[12],inverse[12];rm_affine_product(composed,parent,p->matrices+a->bone*12);
    if(!rm_affine_inverse(inverse,composed))return 0;
    rm_point(composed,a->point[0],a->point[1],a->point[2],position);
    for(int i=0;i<3;i++)forward[i]=composed[i*4]*a->forward[0]+composed[i*4+1]*a->forward[1]+composed[i*4+2]*a->forward[2];
    return rm_unit3(forward);
}
#endif
