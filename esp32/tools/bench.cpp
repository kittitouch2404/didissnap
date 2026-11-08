#include "Arduino.h"
uint32_t g_ms=1000; int g_pin[64];
SerialC Serial; EspC ESP;
#include "LittleFS.h"
FSMem g_fs; LittleFSC LittleFS;
long random(long n){ return 0; }
#define BG_HOST
#include "pages.h"
#include "menu.h"
#include <chrono>
static uint16_t S[480*68], bgi[480*272];
int main(){
  didicfg::cfg.maxRpm=7000; didicfg::cfg.gearCount=5;
  RAW.rpm=5620; RAW.spd=109; RAW.ect=88; RAW.iat=37; RAW.volt=14.2; RAW.load=76; RAW.tps=85; RAW.map=165; RAW.maf=50.6; RAW.fuel=67;
  data::update(.016f,false); D.link=2; perf::begin(); bg::hostImg=bgi;
  for(int p=0;p<16;p++){ didicfg::cfg.page=p;
    auto t0=std::chrono::steady_clock::now(); int N=200;
    for(int k=0;k<N;k++) for(int y0=0;y0<272;y0+=68){ gfx::strip(S,y0,y0+68); pages::draw(p);} 
    double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count()/N;
    printf("page %2d %-12s %7.0f us/frame\n",p+1,pages::NAMES[p],us);}
}
