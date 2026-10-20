/* Export all loaded source composites through the native blitter, without
 * launching the game or inventing frame timing. Outputs stay in ignored work/. */
#define main visitor_snapshot_main
#include "visitor_snapshot.c"
#undef main
int main(int argc,char **argv) {
    if(argc!=5){fprintf(stderr,"usage: source_frames MEMORY PALETTE OUTDIR FRAME_COUNT\n");return 2;}
    int count=atoi(argv[4]);if(count<1 || count>65536)return 2;
    memory=malloc(64u<<20);FILE*f=fopen(argv[1],"rb");
    if(!memory || !f || fread(memory,1,64u<<20,f)!=(64u<<20))return 2;fclose(f);M=memory;
    uint8_t pal[768];f=fopen(argv[2],"rb");if(!f || fread(pal,1,768,f)!=768)return 2;fclose(f);
    for(int i=0;i<256;i++)palette[i]=0xff000000u|(pal[i*3]*255/63)|((pal[i*3+1]*255/63)<<8)|((pal[i*3+2]*255/63)<<16);
    Geom g;if(!geom(&g))return 2;
    uint32_t frames=r32(0x1d3b80),elems=r32(0x1d3b7c),sprites=r32(0x1d3bb8);
    if(frames+(uint64_t)count*8>(64u<<20))return 2;
    Snap*s=&snaps[0];s->g=g;memset(sa,0,sizeof sa);memset(sb,255,sizeof sb);
    memset(memory+r32(THINGS)+200,0,200);memory[r32(THINGS)+204]=24;
    int exported=0,empty=0;
    for(int fi=1;fi<count;fi++) {
        s->ncards=0;s->npix=0;
        Build b={.s=s,.g=&g,.cx=32,.cy=32,.idx=1,.cls=24};
        b.a=(NDTarget){sa,sizeof sa,0,SW_,SW_,SH_,SW_-1,g.lo,M+r32(GHOST_TAB),0};b.b=b.a;b.b.buf=sb;
        uint32_t e=elems+(uint16_t)r16(frames+(uint32_t)fi*8)*10u;
        for(int guard=0;e>elems && guard<256;guard++) {
            if(e+10>(64u<<20))return 3;
            uint32_t spr=sprites+(uint16_t)r16(e);
            if(spr+6>(64u<<20) || r32(spr)>=(64u<<20))return 3;
            if(spr>sprites)card_log(2,(OXS<<g.lo)+r16(e+2),(OYS<<g.lo)+r16(e+4),
                M[spr+4]|M[spr+5]<<8,r32(spr),(uint16_t)r16(e+6),&b);
            e=elems+(uint16_t)r16(e+8)*10u;
        }
        finish(&b);
        int w=1,h=1;Card*c=s->ncards ? &s->cards[0]:NULL;
        if(c){w=c->w;h=c->h;exported++;}else empty++;
        uint32_t*out=malloc((size_t)w*h*4);if(!out)return 4;
        for(int i=0;i<w*h;i++){uint16_t v=c?s->pix[c->off+i]:256;out[i]=v<256?palette[v]:0xff403830;}
        char path[4096];snprintf(path,sizeof path,"%s/M-%d.png",argv[3],fi);
        int ok=png(path,out,w,h);free(out);if(!ok)return 5;
    }
    fprintf(stderr,"%d nonempty composites, %d empty records exported\n",exported,empty);free(memory);return 0;
}
