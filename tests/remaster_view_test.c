/* Synthetic native-pass and normal-view integration; no owned game data. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *mem;
uint8_t *tp_host_memory(void) { return mem; }
void tp_host_palette_rgba(uint32_t pal[256]) { for (int i = 0; i < 256; i++) pal[i] = 0xff000000u | (uint32_t)i; }
int tp_host_png_rgba(const char *p, const uint32_t *px, int w, int h) { (void)p; (void)px; (void)w; (void)h; return 0; }
void tp_host_mouse(double fx, double fy, uint32_t b) { (void)fx; (void)fy; (void)b; }
void tp_host_mouse_pos(int *x, int *y) { *x = *y = 0; }
int64_t tp_host_guest_call(uint32_t fn, const uint32_t *a, int n, uint32_t m) { (void)fn; (void)a; (void)n; (void)m; return -1; }
void tp_host_key(uint32_t k) { (void)k; }
void tp_host_key_raw(uint8_t s, int d) { (void)s; (void)d; }
static uint32_t registers[10];
uint32_t tp_get(unsigned i) { return i<10?registers[i]:0; }
uint32_t tp_hook_pcs[32]; int tp_hook_which; uint64_t tp_insns, tp_rep;
uint32_t tp_map_w = 64, tp_map_ext = 64;   /* (the game's park size; bigmap.c) */
void tp_bigmap_setup_done(void) {}
void tp_free_park_shown(void) {} int tp_free_waiting(void) { return 0; }
void sb16_pump(double t) { (void)t; }
double tp_dbg_run, tp_dbg_gov; int tp_host_timer_behind(void) { return 0; }
double tp_host_card_clock(void) { return 0; }
void tp_hooks_changed(void) {}
volatile double tp_frame_paced_at; double tp_host_cycles(void) { return 10000; }   /* (nightly's pacing) */
int tp_view_point_to_world(double fx, double fy, double *wx, double *wy);
void tp_view_geometry(int *sw, int *sh, int *wh);
void tp_view_enable(int main_game);

#include "../runtime/view.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"RGBA view line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void) {
    mem=calloc(64u<<20,1);CHECK(mem);M=mem;
    uint32_t map=0x300000,table=0x210000,source=0x220000,cache=0x400000;
    memcpy(mem+0x1d3b08,&map,4);
    memcpy(mem+0x1d3b90,&table,4);memcpy(mem+table+6,&source,4);
    memcpy(mem+LAND_CACHE,&cache,4);
    const uint8_t sprite[]={4,1,2,3,4,0,4,5,6,7,8,0};memcpy(mem+source,sprite,sizeof sprite);
    for(int mode=1;mode<=2;mode++){
        G=geo(mode);w16(SCREEN_MODE,mode==1?1:8);w16(0x1d663a,mode==1?48:24);w16(TILE_H,mode==1?32:16);
        mem[table+10]=mode==1?8:4;mem[table+11]=mode==1?4:2;
        clipinfo.ok=1;clipinfo.mode=mode;clipinfo.base=0;clipinfo.pitch=SW;
        clipinfo.clip_w=SW;clipinfo.clip_h=PH;clipinfo.clip_r=SW-1;
        memset(mem+cache,11,(size_t)SW*SH);
        int cx=10,cy=10,bx=cam_ox(cx,cy),by=cam_oy(cy);
        CHECK(tp_remaster_surface_prepare(&hd_work,bx,by,SW,PH,CW,CH));
        tp_host_palette_rgba(hd_palette);
        size_t count=(size_t)hd_work.w*hd_work.h*hd_work.density*hd_work.density;
        for(size_t i=0;i<count;i++)hd_work.pixels[i]=0xffabcdefu;
        hd_rendering=1;hd_failed=0;CHECK(native_pass(cx,cy,4)==0);hd_rendering=0;CHECK(!hd_failed);
        for(int y=PT;y<PB;y++)for(int x=TX0;x<TX1;x++){
            uint32_t actual;CHECK(tp_remaster_surface_sample(&hd_work,bx+x+.125,by+y+.125,&actual));
            CHECK(actual==hd_palette[np_out[y*SW+x]]);
        }
        uint32_t untouched;CHECK(tp_remaster_surface_sample(&hd_work,bx+TX0-1,by+PT,&untouched));CHECK(untouched==0xffabcdefu);
    }
    /* Presentation reads fine colour and respects padded strides, UI and
     * weather priority. A pan outside the published region uses the canvas. */
    hd_pub=hd_work;memset(&hd_work,0,sizeof hd_work);
    size_t count=(size_t)hd_pub.w*hd_pub.h*hd_pub.density*hd_pub.density;
    for(size_t i=0;i<count;i++)hd_pub.pixels[i]=0xff123456u;
    memset(pub_canvas,9,sizeof pub_canvas);for(int i=0;i<MAX_SCREEN;i++)pub_overlay[i]=0xffff;
    memset(pub_panel,5,sizeof pub_panel);tp_host_palette_rgba(pub_pal);
    uint32_t pixels[20*16];for(int i=0;i<320;i++)pixels[i]=0xdeadbeefu;
    Geo g=geo(1);
    ComposeJob job={.out=pixels,.ow=16,.oh=16,.g=g,.z=1,.vx=hd_pub.x+10,.vy=hd_pub.y+10,
        .lw=320,.lpx=1,.lpy=1,.sx=1,.sy=1,.ss=1,.pal=pub_pal,.stride=20,.k=1};
    pub_overlay[4*320+3]=7;compose_band(&job,0);
    CHECK(pixels[4*20+3]==pub_pal[7]);CHECK(pixels[4*20+4]==0xff123456u);
    for(int y=0;y<16;y++)for(int x=16;x<20;x++)CHECK(pixels[y*20+x]==0xdeadbeefu);
    pub_weather_n=1;pub_weather[5*320+4]=12;compose_band(&job,0);
    CHECK(pixels[5*20+4]==pub_pal[12]);CHECK(pixels[4*20+3]==pub_pal[7]);pub_weather_n=0;
    tp_view_out_bgra=1;compose_band(&job,0);CHECK(pixels[4*20+4]==0xff563412u);tp_view_out_bgra=0;
    CHECK(compose_world_sample(0,0,&g,pub_pal)==pub_pal[9]);
    hd_pub.valid=0;CHECK(compose_world_sample(job.vx,job.vy,&g,pub_pal)==pub_pal[9]);
    /* The live placement hooks borrow/restore slot 7 and route uncaptured
     * changes through the ordered companion without touching source memory. */
    G=geo(1);w16(SCREEN_MODE,1);uint32_t pitch=320,sh=200,buf=0x500000,sp=0x700000,ret=0x1145fbu;
    memcpy(mem+DISP_PITCH,&pitch,4);memcpy(mem+DISP_H,&sh,4);memcpy(mem+DISP_BUF,&buf,4);memcpy(mem+sp,&ret,4);
    registers[4]=sp;real_cx=10;real_cy=10;active=1;have_world=1;hd_pending=1;
    CHECK(tp_remaster_surface_prepare(&hd_work,cam_ox(10,10),cam_oy(10),SW,SH,CW,CH));
    memset(hd_work.pixels,0,hd_work.capacity*sizeof(uint32_t));
    tp_hook_pcs[5]=TOOL_PREVIEW;tp_hook_pcs[7]=0x11be5a;tp_hook_which=5;tp_view_hook();
    CHECK(tool_watching&&hd_tool.active&&tp_hook_pcs[7]==TOOL_BLIT&&tp_hook_pcs[6]==ret);
    mem[buf+100*320+100]=55;
    tp_hook_which=6;tp_view_hook();CHECK(!tool_watching&&!hd_tool.active&&tp_hook_pcs[7]==0x11be5a);
    CHECK(tp_hook_pcs[6]==0xffffffffu&&hd_work.valid);
    uint32_t preview_pixel;CHECK(tp_remaster_surface_sample(&hd_work,cam_ox(10,10)+100.125,cam_oy(10)+100.125,&preview_pixel));
    CHECK(preview_pixel==hd_palette[55]);
    tool_capture_begin();CHECK(tool_watching);buf+=320;memcpy(mem+DISP_BUF,&buf,4);
    CHECK(tool_capture_end()&&!hd_work.valid&&tp_hook_pcs[7]==0x11be5a);
    free(hd_work.pixels);
    free(hd_pub.pixels);free(mem);
    puts("remaster view: both native modes, trusted pass clipping, fine colour, UI/weather priority, BGRA, stride and camera bounds passed");return 0;
}
