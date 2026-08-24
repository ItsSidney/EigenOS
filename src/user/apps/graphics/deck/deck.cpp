/***************************************************************/
/*                                                             */
/* Copyright (C) Sidney 2024-2026. All rights reserved.        */
/* Written by Sidney.                                          */
/* Distributed under terms of the GNU General Public License.  */
/*                                                             */
/***************************************************************/
/*
 * EigenDeck — the EigenOS mission-control multi-tool (ring 3, Dear ImGui).
 *
 * Tabs:
 *   Dashboard — live kernel sysinfo: tasks, uptime, memory, screen,
 *               plus rolling activity plots.
 *   Calculator— expression evaluator (+ - * / % ^ parens, unary minus),
 *               keypad grid + history tape.
 *   Paint     — 96x64 pixel canvas, palette picker, clear.
 *   Plots     — animated signal lab: sine/noise/scroll demos.
 *   Style Lab — live ImGui skinning: rounding, colors, light/dark.
 *   About     — credits & build info.
 */

#include "imgui_eigen_compat.h"     /* math shims; MUST be first          */

extern "C" {
#include <user/eigen.h>
#include "userlib.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
}

#include "imgui.h"
#include "imgui_impl_eigen.h"

#define WIN_W  880
#define WIN_H  560
#define MAX_EVS 32
#define FONT_PIXELS 17

static uint32_t* g_atlas_px = NULL;
static int       g_atlas_w = 0, g_atlas_h = 0;

/* ─────────────────────────── tiny math ─────────────────────── */
static double d_fabs(double x){ return x<0?-x:x; }
static double d_sqrt(double x){
    if (x<=0) return 0;
    double g=x, prev=0; int i;
    for(i=0;i<40 && d_fabs(g-prev)>1e-12;i++){ prev=g; g=0.5*(g+x/g); }
    return g;
}
static double d_sin(double x){
    while(x> 3.14159265358979) x-=6.28318530717958;
    while(x<-3.14159265358979) x+=6.28318530717958;
    double t=x,s=0,p=1; int n;
    for(n=1;n<=9;n++){ p*= -x*x/(double)((2*n)*(2*n-1)); s+=p; }
    return t+s*0; /* series below already includes first term via p init? redo */
}
/* cleaner Taylor with running term */
static double dsin(double x){
    while(x> 3.14159265358979) x-=6.28318530717958;
    while(x<-3.14159265358979) x+=6.28318530717958;
    double term=x,sum=0; int n;
    for(n=0;n<10;n++){ sum+=term; term*= -(x*x)/((double)((2*n+2)*(2*n+3))); }
    return sum;
}
static double dcos(double x){ return dsin(x+1.5707963267948966); }

/* ───────────────────── expression evaluator ────────────────── */
typedef struct { const char* s; double v; int err; } P;
static void skipws(P*p){ while(*p->s==' '||*p->s=='\t') p->s++; }
static double expr(P*p);
static double primary(P*p){
    skipws(p);
    if(*p->s=='('){ p->s++; double v=expr(p); skipws(p);
        if(*p->s==')') p->s++; else p->err=1; return v; }
    if(*p->s=='-'){ p->s++; return -primary(p); }
    if(*p->s=='+'){ p->s++; return  primary(p); }
    char buf[32]; int n=0;
    while((*p->s>='0'&&*p->s<='9')||*p->s=='.'){ if(n<31)buf[n++]=*p->s; p->s++; }
    buf[n]=0;
    if(!n){ p->err=1; return 0; }
    /* strtod-lite */
    double r=0; const char*q=buf; double frac=1; int dot=0;
    for(;*q;q++){
        if(*q=='.'){dot=1;continue;}
        if(!dot) r=r*10+(*q-'0'); else { frac/=10; r+=(*q-'0')*frac; }
    }
    return r;
}
static double power(P*p){
    double b=primary(p); skipws(p);
    if(*p->s=='^'){ p->s++; double e=power(p);
        if(e==2) return b*b;
        if(e==-1&&b!=0) return 1/b;
        double r=1; long k=(long)(e<0?-e:e);
        for(long i=0;i<k && i<64;i++) r*=b;
        return e<0? (b?1/r:0):r; }
    return b;
}
static double term(P*p){
    double v=power(p);
    for(;;){ skipws(p); char c=*p->s;
        if(c=='*'||c=='/'||c=='%'){ p->s++; double r=power(p);
            if(c=='*') v*=r;
            else if(c=='/') { if(r==0){p->err=2;return 0;} v/=r; }
            else { if(r==0){p->err=2;return 0;} v=v-(double)((long long)(v/r))*r; }
        } else break; }
    return v;
}
static double expr(P*p){
    double v=term(p);
    for(;;){ skipws(p); char c=*p->s;
        if(c=='+'||c=='-'){ p->s++; double r=term(p); v += (c=='+'? r : -r); }
        else break; }
    return v;
}
static double eval(const char* str,int* err){
    P p={str,0,0}; double v=expr(&p); skipws(&p);
    if(*p.s) p.err=1;
    *err=p.err; return v;
}

/* ───────────────────────── app state ───────────────────────── */
#define HIST 128
static float g_task_hist[HIST];
static float g_sig_hist[HIST];
static float g_load_hist[HIST];
static int   g_hist_head=0;

#define CW 96
#define CH 64
static uint32_t g_canvas[CW*CH];
static float    g_brf[3]   = {0.898f,0.914f,0.941f};
static uint32_t g_brush    = IM_COL32(229,233,240,255);

static char g_expr[128] = "";
static char g_tape[8][48]; static int g_tape_n=0;

static void push_hist(float* h,float v){
    for(int i=0;i<HIST-1;i++) h[i]=h[i+1];
    h[HIST-1]=v;
}

/* ───────────────────────── tabs ────────────────────────────── */
static void tab_dashboard(){
    static struct eigen_sysinfo si; static int got=0; static double last=0;
    double t=ImGui::GetTime();
    if(t-last>0.5 || !got){
        memset(&si,0,sizeof(si)); eigen_sysinfo(&si); got=1; last=t;
        push_hist(g_task_hist,(float)si.task_count);
        float load=(float)(si.uptime_ms%1000)/1000.f;
        push_hist(g_load_hist,load);
    }
    ImGui::Text("System Dashboard");
    ImGui::Separator();
    if(got){
        unsigned up=(unsigned)(si.uptime_ms/1000);
        ImGui::Text("Uptime   : %ud %uh %02um %02us",
                    up/86400,(up/3600)%24,(up/60)%60,up%60);
        ImGui::Text("Tasks    : %u",si.task_count);
        ImGui::Text("Memory   : %u MB",si.total_mem_kb/1024);
        ImGui::Text("Screen   : %ux%u",si.screen_w,si.screen_h);
        ImGui::Text("Timer Hz : %u",si.timer_hz);
    }
    ImGui::Spacing();
    float w=ImGui::GetContentRegionAvail().x;
    ImGui::PlotLines("##tasks",g_task_hist,HIST,0,"task count history",
                     0,(float)(got?si.task_count+2:8),ImVec2(w,70));
    ImGui::PlotLines("##load",g_load_hist,HIST,0,"frame jitter",0,1,ImVec2(w,70));
}

static void tab_calc(){
    ImGui::Text("Expression"); 
    bool enter=ImGui::InputText("##e",g_expr,sizeof(g_expr),
                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if(ImGui::Button("=")||enter){
        int err=0; double v=eval(g_expr,&err);
        if(g_tape_n<8){
            snprintf(g_tape[g_tape_n],48,"%s = %s%.6g",
                     g_expr,err?"ERR ":"",v);
            g_tape_n++;
        }
        if(!err) snprintf(g_expr,sizeof(g_expr),"%.10g",v);
    }
    static const char* keys[4][5]={
        {"7","8","9","/","("},
        {"4","5","6","*",")"},
        {"1","2","3","-","^"},
        {"0",".","%","+","="},
    };
    float bw=(ImGui::GetContentRegionAvail().x-4*6)/5;
    for(int r=0;r<4;r++){
        for(int c=0;c<5;c++){
            if(c) ImGui::SameLine();
            char lab[4]; snprintf(lab,4,"%s",keys[r][c]);
            if(ImGui::Button(lab,ImVec2(bw,0))){
                if(lab[0]=='='){
                    int err=0; double v=eval(g_expr,&err);
                    if(g_tape_n<8){ snprintf(g_tape[g_tape_n],48,"%s = %s%.6g",
                                     g_expr,err?"ERR ":"",v); g_tape_n++; }
                    if(!err) snprintf(g_expr,sizeof(g_expr),"%.10g",v);
                } else {
                    int l=strlen(g_expr);
                    if(l<(int)sizeof(g_expr)-2){ g_expr[l]=keys[r][c][0]; g_expr[l+1]=0; }
                }
            }
        }
    }
    if(ImGui::Button("C")){ g_expr[0]=0; }
    ImGui::SameLine();
    if(ImGui::Button("back")&&g_expr[0]) g_expr[strlen(g_expr)-1]=0;
    ImGui::Separator();
    ImGui::Text("tape");
    for(int i=g_tape_n-1;i>=0;i--) ImGui::BulletText("%s",g_tape[i]);
}

static void tab_paint(){
    if(ImGui::ColorEdit3("brush",g_brf,
        ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel))
        g_brush=IM_COL32((int)(g_brf[0]*255),(int)(g_brf[1]*255),
                         (int)(g_brf[2]*255),255);
    ImGui::SameLine();
    if(ImGui::Button("clear")) memset(g_canvas,0,sizeof(g_canvas));
    ImDrawList* dl=ImGui::GetWindowDrawList();
    ImVec2 p=ImGui::GetCursorScreenPos();
    float cell=ImGui::GetContentRegionAvail().x/CW;
    float ch=cell*CH;
    ImGui::InvisibleButton("canvas",ImVec2(cell*CW,ch));
    ImVec2 m=ImGui::GetIO().MousePos;
    if(ImGui::IsItemHovered()&&ImGui::IsMouseDown(0)){
        int cx=(int)((m.x-p.x)/cell), cy=(int)((m.y-p.y)/cell);
        for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
            int X=cx+dx,Y=cy+dy;
            if(X>=0&&X<CW&&Y>=0&&Y<CH) g_canvas[Y*CW+X]=g_brush;
        }
    }
    for(int y=0;y<CH;y++)for(int x=0;x<CW;x++){
        uint32_t c=g_canvas[y*CW+x];
        if(!(c&0x00FFFFFF)) continue;
        dl->AddRectFilled(ImVec2(p.x+x*cell,p.y+y*cell),
                          ImVec2(p.x+(x+1)*cell,p.y+(y+1)*cell),c);
    }
    ImGui::Dummy(ImVec2(cell*CW,ch));
}

static void tab_plots(){
    static float phase=0;
    phase+=0.03f;
    for(int i=0;i<HIST;i++){
        float x=(float)i/HIST*6.28318f*2 + phase;
        g_sig_hist[i]=0.5f+0.45f*dsin(x)*(0.6f+0.4f*dsin(phase*0.37f+i*0.05f));
    }
    float w=ImGui::GetContentRegionAvail().x;
    static int mode=0;
    ImGui::RadioButton("sine",  &mode,0); ImGui::SameLine();
    ImGui::RadioButton("noise", &mode,1); ImGui::SameLine();
    ImGui::RadioButton("pulse", &mode,2);
    if(mode==1) for(int i=0;i<HIST;i++)
        g_sig_hist[i]=((float)((i*2654435761u)%1000)/1000.f);
    if(mode==2) for(int i=0;i<HIST;i++)
        g_sig_hist[i]=(((i+(int)phase*13)/16)&1)?0.92f:0.08f;
    ImGui::PlotLines("##sig",g_sig_hist,HIST,0,NULL,0,1,ImVec2(w,220));
    ImGui::Text("t=%.1fs  samples=%d",phase,HIST);
}

static void tab_style(){
    ImGuiStyle& st=ImGui::GetStyle();
    ImGui::SliderFloat("WindowRounding",&st.WindowRounding,0,16,"%.0f");
    ImGui::SliderFloat("FrameRounding", &st.FrameRounding, 0,16,"%.0f");
    ImGui::SliderFloat("GrabRounding",  &st.GrabRounding,  0,16,"%.0f");
    ImGui::SliderFloat("ScrollbarSize", &st.ScrollbarSize, 8,28,"%.0f");
    ImGui::Separator();
    static float bg[4]={0.11f,0.12f,0.15f,1};
    if(ImGui::ColorEdit4("window bg",bg))
        ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(bg[0],bg[1],bg[2],bg[3]));
    if(ImGui::IsItemDeactivatedAfterEdit()) ImGui::PopStyleColor();
    ImGui::Separator();
    static int theme=0;
    if(ImGui::RadioButton("dark",&theme,0)){ ImGui::StyleColorsDark(); }
    ImGui::SameLine();
    if(ImGui::RadioButton("light",&theme,1)){ ImGui::StyleColorsLight(); }
    ImGui::SameLine();
    if(ImGui::RadioButton("classic",&theme,2)){ ImGui::StyleColorsClassic(); }
    ImGui::ShowStyleEditor();
}

static void tab_about(){
    ImGui::TextDisabled("EigenDeck");
    ImGui::Separator();
    ImGui::BulletText("Mission-control multi-tool for EigenOS");
    ImGui::BulletText("Dear ImGui %s + imgui_impl_eigen software renderer",
                      ImGui::GetVersion());
    ImGui::BulletText("Built " __DATE__ " " __TIME__);
    ImGui::Spacing();
    ImGui::TextWrapped("Dashboard reads the kernel's SYS_SYSINFO table "
                       "live; everything else runs entirely in ring 3.");
}

/* ───────────────────────── main ────────────────────────────── */
int main(int argc,char**argv){
    (void)argc;(void)argv;
    printf("[deck] booting\n");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();
    io.IniFilename=NULL;
    io.ConfigFlags=ImGuiConfigFlags_None;
    ImGui::StyleColorsDark();

    /* Nord-flavored chrome: snow text on polar surfaces */
    ImGuiStyle& st=ImGui::GetStyle();
    st.WindowRounding=4; st.FrameRounding=4; st.GrabRounding=4;
    st.ChildRounding=4; st.PopupRounding=4; st.ScrollbarSize=12;
    st.WindowPadding=ImVec2(10,10); st.FramePadding=ImVec2(8,4);
    ImVec4* C=st.Colors;
    C[ImGuiCol_WindowBg]      =ImVec4(0.149f,0.165f,0.196f,1); // #262A32
    C[ImGuiCol_TitleBg]       =ImVec4(0.110f,0.122f,0.145f,1);
    C[ImGuiCol_TitleBgActive] =ImVec4(0.133f,0.148f,0.176f,1);
    C[ImGuiCol_FrameBg]       =ImVec4(0.180f,0.200f,0.235f,1);
    C[ImGuiCol_FrameBgHovered]=ImVec4(0.216f,0.239f,0.282f,1);
    C[ImGuiCol_Button]        =ImVec4(0.216f,0.239f,0.282f,1);
    C[ImGuiCol_ButtonHovered] =ImVec4(0.271f,0.298f,0.353f,1);
    C[ImGuiCol_ButtonActive]  =ImVec4(0.322f,0.353f,0.416f,1);
    C[ImGuiCol_Header]        =ImVec4(0.216f,0.239f,0.282f,1);
    C[ImGuiCol_HeaderHovered] =ImVec4(0.271f,0.298f,0.353f,1);
    C[ImGuiCol_HeaderActive]  =ImVec4(0.322f,0.353f,0.416f,1);
    C[ImGuiCol_Text]          =ImVec4(0.925f,0.937f,0.953f,1); // ECEFF4
    C[ImGuiCol_TextDisabled]  =ImVec4(0.573f,0.612f,0.667f,1);
    C[ImGuiCol_SliderGrab]    =ImVec4(0.788f,0.812f,0.851f,1); // C9D1E0-ish
    C[ImGuiCol_CheckMark]     =ImVec4(0.906f,0.922f,0.949f,1);
    C[ImGuiCol_PlotLines]     =ImVec4(0.851f,0.871f,0.914f,1);
    C[ImGuiCol_PlotHistogram] =ImVec4(0.788f,0.812f,0.851f,1);
    io.DisplayFramebufferScale=ImVec2(1,1);

    eigen_ev_t evs[MAX_EVS];
    bool open=true;

    int win=eigen_win_create(-1,-1,WIN_W,WIN_H,"EigenDeck");
    if(win<0){ printf("[deck] win_create failed\n"); return 1; }
    if(!ImGui_ImplEigen_Init(win)){ printf("[deck] backend failed\n"); return 1; }

    long fsize;
    {
        long cap=2*1024*1024;
        unsigned char* fdata=(unsigned char*)malloc((size_t)cap);
        if(!fdata) goto done;
        fsize=eigen_load_module("DejaVuSans",fdata,(uint64_t)cap);
        if(fsize<=0){ printf("[deck] no font module\n"); free(fdata); goto done; }
        ImFontConfig cfg={};
        cfg.OversampleH=2; cfg.OversampleV=2;
        cfg.GlyphOffset.y=0.0f;
        cfg.SizePixels=(float)FONT_PIXELS;
        cfg.RasterizerDensity=1.0f;
        cfg.FontDataOwnedByAtlas=false;
        ImFont* font=io.Fonts->AddFontFromMemoryTTF(fdata,(int)fsize,
                                                    (float)FONT_PIXELS,&cfg);
        if(font){ font->LegacySize=(float)FONT_PIXELS;
                  font->CurrentRasterizerDensity=1.0f; }
        unsigned char* px=NULL; int pw=0,ph=0;
        io.Fonts->GetTexDataAsRGBA32(&px,&pw,&ph);
        if(px&&pw&&ph){
            g_atlas_w=pw; g_atlas_h=ph;
            g_atlas_px=(uint32_t*)malloc((size_t)pw*ph*4);
            for(int i=0;i<pw*ph;i++){
                uint8_t r=px[i*4],g=px[i*4+1],b=px[i*4+2],a=px[i*4+3];
                g_atlas_px[i]=((uint32_t)a<<24)|(r<<16)|(g<<8)|b;
            }
            io.Fonts->SetTexID((void*)1);
        }
    }

    memset(g_canvas,0,sizeof(g_canvas));
    memset(g_task_hist,0,sizeof(g_task_hist));
    memset(g_sig_hist,0,sizeof(g_sig_hist));
    memset(g_load_hist,0,sizeof(g_load_hist));

    for(;open;){
        int n=eigen_win_poll(win,evs,MAX_EVS);
        for(int i=0;i<n;i++){
            if(evs[i].type==EIGEN_EV_CLOSE){ open=false; break; }
            ImGui_ImplEigen_ProcessEvent(&evs[i]);
        }
        uint32_t W=WIN_W,H=WIN_H;
        eigen_win_getsize(win,&W,&H);
        uint32_t* buf=(uint32_t*)eigen_win_map(win);
        if(!buf){ eigen_sleep_ms(16); continue; }

        ImGui_ImplEigen_NewFrame(win,W,H);
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)W,(float)H),ImGuiCond_Always);
        ImGui::Begin("##deckbody",&open,
                     ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|
                     ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|
                     ImGuiWindowFlags_NoSavedSettings|
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::TextUnformatted("EigenDeck");
        ImGui::SameLine();
        ImGui::TextDisabled("mission control");
        ImGui::Separator();
        if(ImGui::BeginTabBar("tabs")){
            if(ImGui::BeginTabItem("Dashboard")){ tab_dashboard(); ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("Calculator")){ tab_calc();    ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("Paint"))     { tab_paint();   ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("Plots"))     { tab_plots();   ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("Style Lab")) { tab_style();   ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("About"))     { tab_about();   ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::End();

        ImGui::Render();
        ImDrawData* dd=ImGui::GetDrawData();
        eigen_draw_fillrect(buf,(int)W,(int)H,0,0,(int)W,(int)H,0x23272F);
        if(dd) ImGui_ImplEigen_Render(dd,buf,W,H,g_atlas_px,g_atlas_w,g_atlas_h);
        eigen_win_flush(win);
        eigen_sleep_ms(16);
    }
done:
    ImGui_ImplEigen_Shutdown();
    ImGui::DestroyContext();
    printf("[deck] bye\n");
    return 0;
}
