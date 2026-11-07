// =====================================================================
//  N1V65Config.h — ค่าตั้งของจอ ESP32 แก้ได้ 2 ทาง
//    1) แอปมือถือ OBD2_Didissnap: MENU → จอ ESP32   (ผ่าน TCP เดียวกับ N1V65Link.h)
//    2) หน้าเว็บในบอร์ด: ต่อ WiFi "N1V65-DASH" แล้วเปิด http://192.168.4.1
//  ค่าตั้งเก็บใน Preferences (namespace เดิมของสเก็ตช์) ปิดเครื่องแล้วไม่หาย
//
//  ใช้คู่กับ N1V65Link.h:
//    #include "N1V65Link.h"
//    #include "N1V65Config.h"
//    setup(): n1link::begin();
//             n1cfg::onApply = applySettings;   // ค่าตั้งเปลี่ยน → ใช้ค่าใหม่ (ความสว่าง, กลับจอ ...)
//             n1cfg::onPage  = gotoPage;        // แอป/เว็บ/สลับอัตโนมัติ สั่งเปลี่ยนหน้า
//             n1cfg::begin(prefs, PAGE_NAMES, N_PAGES);
//    loop():  n1link::loop(); n1cfg::loop(); n1link::send(...);
//  อ่านค่าตั้งจาก n1cfg::cfg.maxRpm, n1cfg::cfg.tempAlert, ... ได้ตลอด
// =====================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include "N1V65Link.h"

namespace n1cfg {

struct Config {
  int maxRpm = 7500;      // redline / ไฟ shift
  int tempAlert = 100;    // เตือนน้ำ (°C)
  int bri = 80;           // ความสว่าง 10-100 %
  int briAuto = 0;        // 1 = ปรับตามแสง (ถ้าบอร์ดมีเซ็นเซอร์)
  int flip = 0;           // 1 = กลับจอ 180°
  int page = 0;           // หน้าที่แสดง
  int autoCycle = 0;      // สลับหน้าเองทุก N วินาที (0 = ปิด)
  int gearCount = 6;      // จำนวนเกียร์
  float gears[6] = {118, 68, 47, 36, 29, 24};   // rpm ต่อ 1 km/h ของแต่ละเกียร์
};

static Config cfg;
static Preferences* prefs = nullptr;
static const char* const* pageNames = nullptr;
static int pageCount = 1;

typedef void (*Fn)();
static Fn onApply = nullptr;        // ค่าตั้งเปลี่ยน
static Fn onPeaksReset = nullptr;   // สั่งล้างค่าสูงสุด
static Fn onTripReset = nullptr;    // สั่งรีเซ็ตทริป
static void (*onPage)(int) = nullptr;

static WebServer web(80);
static uint32_t lastCycle = 0;

// ------------------------------------------------------------------ เก็บ/โหลด
inline void load() {
  if (!prefs) return;
  cfg.maxRpm = constrain(prefs->getInt("maxRpm", cfg.maxRpm), 2000, 12000);
  cfg.tempAlert = constrain(prefs->getInt("tempAlert", cfg.tempAlert), 70, 130);
  cfg.bri = constrain(prefs->getInt("bri", cfg.bri), 10, 100);
  cfg.briAuto = prefs->getBool("briAuto", cfg.briAuto) ? 1 : 0;
  cfg.flip = prefs->getBool("flip", cfg.flip) ? 1 : 0;
  cfg.page = constrain(prefs->getInt("page", cfg.page), 0, pageCount - 1);
  cfg.autoCycle = constrain(prefs->getInt("autoCyc", 0), 0, 120);
  cfg.gearCount = constrain(prefs->getInt("gcnt", cfg.gearCount), 3, 6);
  for (int g = 0; g < 6; g++) {
    char k[5] = {'g', 'r', (char)('0' + g), 0, 0};
    float v = prefs->getFloat(k, cfg.gears[g]);
    if (v >= 5.0f && v <= 400.0f) cfg.gears[g] = v;
  }
}

inline void save() {
  if (!prefs) return;
  prefs->putInt("maxRpm", cfg.maxRpm);
  prefs->putInt("tempAlert", cfg.tempAlert);
  prefs->putInt("bri", cfg.bri);
  prefs->putBool("briAuto", cfg.briAuto != 0);
  prefs->putBool("flip", cfg.flip != 0);
  prefs->putInt("page", cfg.page);
  prefs->putInt("autoCyc", cfg.autoCycle);
  prefs->putInt("gcnt", cfg.gearCount);
  for (int g = 0; g < 6; g++) {
    char k[5] = {'g', 'r', (char)('0' + g), 0, 0};
    prefs->putFloat(k, cfg.gears[g]);
  }
}

// ------------------------------------------------------------------ JSON
inline String toJson() {
  String s;
  s.reserve(420);
  s += "{\"cfg\":{\"maxRpm\":";
  s += cfg.maxRpm;
  s += ",\"tempAlert\":";
  s += cfg.tempAlert;
  s += ",\"bri\":";
  s += cfg.bri;
  s += ",\"briAuto\":";
  s += cfg.briAuto;
  s += ",\"flip\":";
  s += cfg.flip;
  s += ",\"page\":";
  s += cfg.page;
  s += ",\"autoCycle\":";
  s += cfg.autoCycle;
  s += ",\"gearCount\":";
  s += cfg.gearCount;
  for (int g = 0; g < 6; g++) {
    s += ",\"g";
    s += g;
    s += "\":";
    s += String(cfg.gears[g], 1);
  }
  s += "},\"pages\":[";
  for (int i = 0; i < pageCount; i++) {
    if (i) s += ",";
    s += "\"";
    s += (pageNames && pageNames[i]) ? pageNames[i] : "PAGE";
    s += "\"";
  }
  s += "]}";
  return s;
}

// ตั้งค่าทีละตัวตามชื่อ คืน true ถ้ารู้จักชื่อ
inline bool setKV(const String& k, float v) {
  int iv = (int)lroundf(v);
  if (k == "maxRpm") cfg.maxRpm = constrain(iv, 2000, 12000);
  else if (k == "tempAlert") cfg.tempAlert = constrain(iv, 70, 130);
  else if (k == "bri") cfg.bri = constrain(iv, 10, 100);
  else if (k == "briAuto") cfg.briAuto = iv ? 1 : 0;
  else if (k == "flip") cfg.flip = iv ? 1 : 0;
  else if (k == "page") cfg.page = constrain(iv, 0, pageCount - 1);
  else if (k == "autoCycle") cfg.autoCycle = constrain(iv, 0, 120);
  else if (k == "gearCount") cfg.gearCount = constrain(iv, 3, 6);
  else if (k.length() == 2 && k[0] == 'g' && k[1] >= '0' && k[1] <= '5') {
    if (v >= 5.0f && v <= 400.0f) cfg.gears[k[1] - '0'] = v;
  } else return false;
  return true;
}

static const char* const KEYS[] = {"maxRpm", "tempAlert", "bri", "briAuto", "flip", "page",
                                   "autoCycle", "gearCount", "g0", "g1", "g2", "g3", "g4", "g5"};
static const int N_KEYS = sizeof(KEYS) / sizeof(KEYS[0]);

// หาเลขหลัง "key": ในข้อความ JSON แบบง่าย (ไม่ต้องใช้ไลบรารี JSON)
inline bool numField(const String& s, const char* key, float& out) {
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k);
  if (i < 0) return false;
  i += k.length();
  while (i < (int)s.length() && s[i] == ' ') i++;
  if (i >= (int)s.length()) return false;
  char c = s[i];
  if (!(c == '-' || c == '.' || (c >= '0' && c <= '9'))) return false;
  out = s.substring(i).toFloat();
  return true;
}

// หลังค่าตั้งเปลี่ยน: บันทึก + บอกสเก็ตช์
inline void changed(int oldPage) {
  save();
  if (cfg.page != oldPage && onPage) onPage(cfg.page);
  if (onApply) onApply();
  lastCycle = millis();
}

// เปลี่ยนหน้าจากที่อื่น (ปุ่ม/แอป/เว็บ) แล้วจำไว้
inline void setPage(int p) {
  if (pageCount <= 0) return;
  p = ((p % pageCount) + pageCount) % pageCount;
  int old = cfg.page;
  cfg.page = p;
  if (prefs) prefs->putInt("page", p);
  if (p != old && onPage) onPage(p);
  lastCycle = millis();
}

// ------------------------------------------------------------------ คำสั่งจากแอป (TCP)
inline bool handleLine(const String& line) {
  if (line.indexOf("\"get_cfg\"") >= 0) {
    n1link::reply(toJson());
    return true;
  }
  if (line.indexOf("\"set_cfg\"") >= 0) {
    int old = cfg.page;
    for (int i = 0; i < N_KEYS; i++) {
      float v;
      if (numField(line, KEYS[i], v)) setKV(KEYS[i], v);
    }
    changed(old);
    n1link::reply(toJson());
    return true;
  }
  if (line.indexOf("\"page\"") >= 0 && line.indexOf("\"cmd\"") >= 0) {
    float v;
    if (numField(line, "v", v)) setPage((int)v);
    n1link::reply(toJson());
    return true;
  }
  if (line.indexOf("\"peaks_reset\"") >= 0) {
    if (onPeaksReset) onPeaksReset();
    n1link::reply("{\"ok\":true}");
    return true;
  }
  if (line.indexOf("\"trip_reset\"") >= 0) {
    if (onTripReset) onTripReset();
    n1link::reply("{\"ok\":true}");
    return true;
  }
  return false;   // ไม่ใช่คำสั่งของเรา (เช่น dtc_read) ให้ N1V65Link จัดการต่อ
}

// ------------------------------------------------------------------ หน้าเว็บ
static const char PAGE_HTML[] = R"HTML(<!DOCTYPE html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>N1V65 ตั้งค่าจอ</title>
<style>
body{margin:0;background:#0b0b0b;color:#e8e8e8;font-family:system-ui,sans-serif}
header{background:#1a1512;padding:12px 16px;color:#FF7A00;font-weight:800;font-size:18px;position:sticky;top:0}
.live{display:flex;gap:8px;flex-wrap:wrap;padding:10px 12px;background:#111}
.live div{background:#1b1b1b;border-radius:8px;padding:6px 10px;min-width:64px}
.live b{display:block;font-size:20px}.live small{color:#888;font-size:11px}
section{padding:6px 16px 12px;border-bottom:1px solid #222}
h3{color:#FF7A00;font-size:14px;margin:12px 0 6px}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:7px 0}
.row span{flex:1}
input[type=number]{width:90px;padding:8px;border-radius:8px;border:1px solid #444;background:#161616;color:#fff;font-size:16px;text-align:center}
select{padding:8px;border-radius:8px;background:#161616;color:#fff;border:1px solid #444;font-size:15px;max-width:60%}
input[type=range]{flex:1;accent-color:#FF7A00}
.sw{width:48px;height:26px;accent-color:#FF7A00}
button{background:#FF7A00;color:#1a0d00;border:0;border-radius:20px;padding:10px 16px;font-weight:700;font-size:14px;margin:4px 4px 4px 0}
button.o{background:none;border:1px solid #888;color:#ddd}
#msg{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);background:#333;padding:10px 16px;border-radius:8px;opacity:0;transition:.3s}
.codes div{padding:6px 0;font-size:18px;font-weight:700;color:#FFC400}
</style></head><body>
<header>N1V65 · ตั้งค่าจอ ESP32</header>
<div class="live" id="live"></div>
<section><h3>หน้าจอ</h3>
<div class="row"><span>หน้าที่แสดง</span><select id="page"></select></div>
<div class="row"><span>สลับหน้าเองทุก (วินาที, 0 = ปิด)</span><input type="number" id="autoCycle" min="0" max="120"></div>
<div class="row"><span>ความสว่าง <b id="briV"></b></span><input type="range" id="bri" min="10" max="100" step="10"></div>
<div class="row"><span>ปรับความสว่างตามแสงอัตโนมัติ</span><input type="checkbox" class="sw" id="briAuto"></div>
<div class="row"><span>กลับจอ 180°</span><input type="checkbox" class="sw" id="flip"></div></section>
<section><h3>รอบ / ความร้อน</h3>
<div class="row"><span>Redline (rpm)</span><input type="number" id="maxRpm" min="2000" max="12000" step="250"></div>
<div class="row"><span>เตือนน้ำที่ (°C)</span><input type="number" id="tempAlert" min="70" max="130"></div></section>
<section><h3>เกียร์ (rpm ต่อ 1 km/h)</h3>
<div class="row"><span>จำนวนเกียร์</span><input type="number" id="gearCount" min="3" max="6"></div>
<div id="gears"></div></section>
<section><h3>DTC / รีเซ็ต</h3>
<button onclick="dtc()">อ่าน DTC</button><button class="o" onclick="dtcClear()">ลบ DTC</button>
<div class="codes" id="codes"></div>
<button class="o" onclick="act('peaks_reset','ล้างค่าสูงสุดแล้ว')">ล้างค่าสูงสุด</button>
<button class="o" onclick="act('trip_reset','รีเซ็ตทริปแล้ว')">รีเซ็ตทริป</button></section>
<div id="msg"></div>
<script>
const $=i=>document.getElementById(i);let C={};
function msg(t){const m=$('msg');m.textContent=t;m.style.opacity=1;clearTimeout(msg.t);msg.t=setTimeout(()=>m.style.opacity=0,1800)}
function fill(j){C=j.cfg;const p=$('page');p.innerHTML=j.pages.map((n,i)=>`<option value="${i}">${i+1}. ${n}</option>`).join('');p.value=C.page;
['maxRpm','tempAlert','autoCycle','gearCount','bri'].forEach(k=>$(k).value=C[k]);$('briV').textContent=C.bri+'%';
$('briAuto').checked=!!C.briAuto;$('flip').checked=!!C.flip;
let g='';for(let i=0;i<C.gearCount;i++)g+=`<div class="row"><span>เกียร์ ${i+1}</span><input type="number" step="0.1" id="g${i}" value="${C['g'+i]}"></div>`;$('gears').innerHTML=g;
for(let i=0;i<C.gearCount;i++)$('g'+i).onchange=e=>set('g'+i,e.target.value)}
async function load(){fill(await (await fetch('/api/cfg')).json())}
async function set(k,v){const r=await fetch('/api/set?'+k+'='+encodeURIComponent(v));fill(await r.json());msg('บันทึกแล้ว')}
['maxRpm','tempAlert','autoCycle','gearCount','page'].forEach(k=>$(k).onchange=e=>set(k,e.target.value));
$('bri').oninput=e=>$('briV').textContent=e.target.value+'%';$('bri').onchange=e=>set('bri',e.target.value);
$('briAuto').onchange=e=>set('briAuto',e.target.checked?1:0);$('flip').onchange=e=>set('flip',e.target.checked?1:0);
async function dtc(){$('codes').textContent='กำลังอ่าน...';const j=await (await fetch('/api/dtc')).json();$('codes').innerHTML=j.dtc.length?j.dtc.map(c=>`<div>⚠ ${c}</div>`).join(''):'<div style="color:#00E05A">ไม่พบรหัส DTC ✓</div>'}
async function dtcClear(){if(!confirm('ลบรหัส DTC และดับไฟ Check Engine?'))return;const j=await (await fetch('/api/dtc_clear')).json();msg(j.cleared?'ลบแล้ว':'ECU ไม่ยืนยัน');if(j.cleared)$('codes').innerHTML=''}
async function act(a,t){await fetch('/api/'+a);msg(t)}
const L=[['rpm','รอบ',''],['spd','km/h',''],['ect','น้ำ','°C'],['iat','IAT','°C'],['volt','แบต','V'],['load','Load','%'],['tps','TPS','%'],['map','MAP','kPa'],['fuel','น้ำมัน','%']];
async function live(){try{const j=await (await fetch('/api/live')).json();$('live').innerHTML=L.map(([k,n,u])=>`<div><small>${n}</small><b>${j[k]!==undefined?j[k]:'--'}${u}</b></div>`).join('')}catch(e){}setTimeout(live,700)}
load();live();
</script></body></html>)HTML";

inline void setupWeb() {
  web.on("/", []() { web.send(200, "text/html; charset=utf-8", PAGE_HTML); });
  web.on("/api/cfg", []() { web.send(200, "application/json", toJson()); });
  web.on("/api/set", []() {
    int old = cfg.page;
    for (int i = 0; i < web.args(); i++) setKV(web.argName(i), web.arg(i).toFloat());
    changed(old);
    web.send(200, "application/json", toJson());
  });
  web.on("/api/live", []() { web.send(200, "application/json", n1link::lastJson); });
  web.on("/api/dtc", []() {
    String arr = n1link::onDtcRead ? n1link::onDtcRead() : String("[]");
    web.send(200, "application/json", "{\"dtc\":" + arr + "}");
  });
  web.on("/api/dtc_clear", []() {
    bool ok = n1link::onDtcClear ? n1link::onDtcClear() : false;
    web.send(200, "application/json", ok ? "{\"cleared\":true}" : "{\"cleared\":false}");
  });
  web.on("/api/peaks_reset", []() {
    if (onPeaksReset) onPeaksReset();
    web.send(200, "application/json", "{\"ok\":true}");
  });
  web.on("/api/trip_reset", []() {
    if (onTripReset) onTripReset();
    web.send(200, "application/json", "{\"ok\":true}");
  });
  web.onNotFound([]() { web.send(404, "text/plain", "not found"); });
  web.begin();
}

// เรียกใน setup() หลัง n1link::begin()
inline void begin(Preferences& p, const char* const* names, int count) {
  prefs = &p;
  pageNames = names;
  pageCount = count > 0 ? count : 1;
  load();
  n1link::onCommand = handleLine;
  setupWeb();
  lastCycle = millis();
}

// เรียกทุกรอบใน loop()
inline void loop() {
  web.handleClient();
  if (cfg.autoCycle > 0 && millis() - lastCycle >= (uint32_t)cfg.autoCycle * 1000UL) {
    int p = (cfg.page + 1) % pageCount;
    cfg.page = p;                 // สลับอัตโนมัติไม่บันทึกลงแฟลชทุกครั้ง (ถนอมแฟลช)
    if (onPage) onPage(p);
    lastCycle = millis();
  }
}

}  // namespace n1cfg
