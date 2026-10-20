/* Bounded development observer for the headless native host. Never modifies
 * simulation memory. Samples changed records at most 50 times/second; timing
 * evidence is observational, not proof that every transition was captured. */
#ifndef TP_ANIMATION_TRACE_H
#define TP_ANIMATION_TRACE_H
extern uint8_t *tp_host_memory(void);
static void tp_animation_trace_sample(double elapsed) {
    const char *path=getenv("TP_ANIMATION_TRACE");if(!path || !*path)return;
    double start=getenv("TP_ANIMATION_TRACE_START")?atof(getenv("TP_ANIMATION_TRACE_START")):0;
    static double next; if(elapsed<start || elapsed<next)return;next=elapsed+.02;
    static FILE*f;static int failed;static uint32_t prev[12000][19];static unsigned lines;
    if(failed || lines>=1000000)return;
    if(!f){f=fopen(path,"w");if(!f){failed=1;return;}setvbuf(f,NULL,_IOLBF,0);
        fprintf(f,"seconds,tick,thing,class,state,variant,frame,x,y,frac_x,frac_y,direction,record_offset,phase,speed,rider_link,overlay,extra_a,extra_b,lift\n");}
    uint8_t *m=tp_host_memory();uint32_t base,last,tick;
    memcpy(&base,m+0x1d3b10,4);memcpy(&last,m+0x1d3b78,4);memcpy(&tick,m+0x1d3afc,4);
    if(base<0x200000 || base>=(64u<<20) || last<base || last>=(64u<<20))return;
    unsigned count=(last-base)/200+1;if(count>12000)count=12000;
    for(unsigned i=1;i<count;i++) {
        if(base+(uint64_t)(i+1)*200>(64u<<20))break;
        const uint8_t*t=m+base+i*200;uint16_t frame,off,speed,link,overlay,aa,bb;
        uint32_t phase;memcpy(&frame,t+10,2);memcpy(&off,t+22,2);memcpy(&phase,t+32,4);
        memcpy(&speed,t+46,2);memcpy(&link,t+71,2);memcpy(&overlay,t+142,2);memcpy(&aa,t+135,2);memcpy(&bb,t+137,2);
        uint32_t cur[19]={i,t[4],t[12],t[89],frame,t[7],t[9],t[6],t[8],t[18],off,phase,speed,link,overlay,aa,bb,t[177],0};
        if(!t[4] && !prev[i][1])continue;
        if(!memcmp(cur,prev[i],sizeof cur))continue;memcpy(prev[i],cur,sizeof cur);
        fprintf(f,"%.6f,%u",elapsed,tick);for(int k=0;k<18;k++)fprintf(f,",%u",cur[k]);fputc('\n',f);lines++;
    }
}
#endif
