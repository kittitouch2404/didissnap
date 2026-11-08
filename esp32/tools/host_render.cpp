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
static uint16_t fbuf[480*272];
int main(int argc,char**argv){
  gfx::target(fbuf);
  didicfg::cfg.maxRpm=7000; didicfg::cfg.gearCount=5; float g[6]={131.6,74.6,51.0,38.6,30.9,26}; for(int i=0;i<6;i++)didicfg::cfg.gears[i]=g[i];
  RAW.rpm=5620; RAW.spd=109; RAW.ect=88; RAW.iat=37; RAW.volt=14.2; RAW.load=76; RAW.tps=85; RAW.map=165; RAW.maf=50.6; RAW.fuel=67; RAW.stft=-2.1;
  data::update(.016f,false); D.trip=36.2; D.pRpm=6950; D.pSpd=142; D.pEct=90; D.pIat=39; D.pVmin=13.9; D.link=2;
  // background image (raw rgb565 from python) for pages 9-12
  FILE* f=fopen("/tmp/host/bg.raw","rb"); static uint16_t bgi[480*272]; if(f){fread(bgi,2,480*272,f);fclose(f);bg::hostImg=bgi;}
  if(argc>4){didicfg::cfg.p13a=atoi(argv[1]);didicfg::cfg.p13b=atoi(argv[2]);didicfg::cfg.p13c=atoi(argv[3]);didicfg::cfg.p13d=atoi(argv[4]);}
  perf::begin(); perf::state=perf::DONE; float tt[7]={2.18,4.62,8.94,4.32,-1,9.71,15.42}, bb[7]={2.11,4.55,8.80,4.25,-1,9.66,15.31}; for(int i=0;i<7;i++){perf::times[i]=tt[i];perf::best[i]=bb[i];} perf::traps[0]=38; perf::traps[5]=118; perf::traps[6]=141; perf::el=15.42; perf::dist=402; perf::top=141; perf::runs=3;
  for(int p=0;p<16;p++){ didicfg::cfg.page=p; pages::draw(p); char n[64]; snprintf(n,64,"/tmp/host/p%02d.raw",p); FILE*o=fopen(n,"wb"); fwrite(fbuf,2,480*272,o); fclose(o);}
  menu::open=true; menu::sel=3; menu::draw(); FILE*o=fopen("/tmp/host/menu.raw","wb"); fwrite(fbuf,2,480*272,o); fclose(o);
  printf("ok\n");
}
