#include "Arduino.h"
uint32_t g_ms=1000; int g_pin[64];
SerialC Serial; EspC ESP;
#include "LittleFS.h"
FSMem g_fs; LittleFSC LittleFS;
long random(long n){ return 0; }
#define BG_HOST
#include "pages.h"
#include "menu.h"
#include <cstdio>
static uint16_t A[480*272], B[480*272], S[480*68];
int main(){
  didicfg::cfg.maxRpm=7000; didicfg::cfg.gearCount=5;
  RAW.rpm=5620; RAW.spd=109; RAW.ect=88; RAW.iat=37; RAW.volt=14.2; RAW.load=76; RAW.tps=85; RAW.map=165; RAW.maf=50.6; RAW.fuel=67;
  data::update(.016f,false); D.link=2; perf::begin();
  static uint16_t bgi[480*272]; for(int i=0;i<480*272;i++)bgi[i]=i*7; bg::hostImg=bgi;
  int bad=0;
  for(int p=0;p<17;p++){
    bool m=p==16; menu::open=m; didicfg::cfg.page=m?0:p;
    gfx::target(A); if(m)menu::draw(); else pages::draw(p);
    for(int y0=0;y0<272;y0+=68){ gfx::strip(S,y0,y0+68); if(m)menu::draw(); else pages::draw(p); memcpy(B+y0*480,S,68*480*2);}
    int diff=0; for(int i=0;i<480*272;i++) if(A[i]!=B[i]) diff++;
    printf("page %d diff %d\n",p+1,diff); if(diff)bad++;
  }
  printf(bad?"STRIP MISMATCH\n":"STRIPS IDENTICAL\n");
}
