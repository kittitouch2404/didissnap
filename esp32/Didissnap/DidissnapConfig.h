// =====================================================================
//  DidissnapConfig.h — ค่าตั้งของจอ ESP32 แก้ได้ 2 ทาง
//    1) แอปมือถือ OBD2_Didissnap: MENU → จอ ESP32   (ผ่าน TCP เดียวกับ DidissnapLink.h)
//    2) หน้าเว็บในบอร์ด: ต่อ WiFi "Didissnap" แล้วเปิด http://192.168.4.1
//  ค่าตั้งเก็บใน Preferences (namespace เดิมของสเก็ตช์) ปิดเครื่องแล้วไม่หาย
//
//  ใช้คู่กับ DidissnapLink.h:
//    #include "DidissnapLink.h"
//    #include "DidissnapConfig.h"
//    setup(): didilink::begin();
//             didicfg::onApply = applySettings;   // ค่าตั้งเปลี่ยน → ใช้ค่าใหม่ (ความสว่าง, กลับจอ ...)
//             didicfg::onPage  = gotoPage;        // แอป/เว็บ/สลับอัตโนมัติ สั่งเปลี่ยนหน้า
//             didicfg::begin(prefs, PAGE_NAMES, N_PAGES);
//    loop():  didilink::loop(); didicfg::loop(); didilink::send(...);
//  อ่านค่าตั้งจาก didicfg::cfg.maxRpm, didicfg::cfg.tempAlert, ... ได้ตลอด
// =====================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include "DidissnapLink.h"

namespace didicfg {

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
  int p13a = 0, p13b = 1, p13c = 2, p13d = 3;
  int p13spd = 1;
  // ปรับค่าให้ตรง: เกน % (100 = ไม่ปรับ) · ออฟเซ็ต · TPS จุด 0/100 (ค่าดิบจากรถ %)
  int spdG = 100, rpmG = 100, afrG = 100, ectO = 0, iatO = 0, vOfs = 0 /* x0.1 V */, tps0 = 0, tps1 = 100;
  int picLay = 0;                                 // หน้า 14-16 แบบหน้า 13 (บิต 0 = หน้า 14, 1 = 15, 2 = 16)
  int needle = 0;                                 // แบบเข็ม (pages::NEEDLE_NAMES)
  int anim = 1;                                   // อนิเมชันเกจ (กวาดเข็มตอนเปิด สปริง ค้างค่าสูงสุด สี ไฟ shift)
  int pgMask = 0xFFFF;                            // หน้าที่เปิดใช้ (บิต 0 = หน้า 1) ปิดแล้วกดปุ่ม/สลับอัตโนมัติจะข้าม                                 // หน้า 13: โชว์ความเร็วมุมขวาบน (1 = เปิด)   // หน้ารูป (13): ค่าที่โชว์ในแถวล่าง 4 ช่อง (ลำดับใน SLOT_NAMES)
};
// ค่าที่เลือกโชว์ในช่องล่างของหน้า 13 ได้
static const char* const SLOT_NAMES[] = {"COOLANT", "IAT", "BATT", "BOOST", "LOAD", "TPS", "MAF", "FUEL", "SPEED", "TRIP", "STFT", "AFR"};
static const int N_SLOTS = sizeof(SLOT_NAMES) / sizeof(SLOT_NAMES[0]);

static Config cfg;
// ค่าที่ปรับได้: ชื่อ, ต่ำสุด, สูงสุด, ค่าเริ่มต้น
struct CalK { const char* k; int mn, mx, def; int Config::*f; };
static const CalK CALS[] = {{"spdG", 80, 130, 100, &Config::spdG}, {"rpmG", 80, 120, 100, &Config::rpmG},
                            {"afrG", 80, 120, 100, &Config::afrG}, {"ectO", -15, 15, 0, &Config::ectO},
                            {"iatO", -15, 15, 0, &Config::iatO},   {"vOfs", -20, 20, 0, &Config::vOfs},
                            {"tps0", 0, 60, 0, &Config::tps0},     {"tps1", 30, 100, 100, &Config::tps1}};
static const int N_CALS = sizeof(CALS) / sizeof(CALS[0]);
static float (*rawTps)() = nullptr;   // ค่า TPS ดิบตอนนี้ (ตั้งจากสเก็ตช์) — ใช้กับ "ตั้งตอนนี้เป็น 0% / 100%"
inline void calFix() { if (cfg.tps1 < cfg.tps0 + 10) cfg.tps1 = min(100, cfg.tps0 + 10); if (cfg.tps1 < cfg.tps0 + 10) cfg.tps0 = cfg.tps1 - 10; }
inline void calReset() { for (int i = 0; i < N_CALS; i++) cfg.*(CALS[i].f) = CALS[i].def; }
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
static uint32_t saveAt = 0;     // บันทึกลงแฟลชทีหลัง (กดปุ่มรัว ๆ ไม่ต้องเขียนแฟลชทุกครั้ง → จอไม่สะดุด)

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
  cfg.p13a = constrain(prefs->getInt("p13a", cfg.p13a), 0, N_SLOTS - 1);
  cfg.p13spd = prefs->getInt("p13spd", cfg.p13spd) ? 1 : 0;
  cfg.anim = prefs->getInt("anim", cfg.anim) ? 1 : 0;
  cfg.picLay = prefs->getInt("picLay", 0) & 7;
  cfg.needle = constrain(prefs->getInt("needle", cfg.needle), 0, 5);
  cfg.pgMask = prefs->getInt("pgMask", cfg.pgMask) & 0xFFFF;
  if (!cfg.pgMask) cfg.pgMask = 0xFFFF;
  cfg.p13b = constrain(prefs->getInt("p13b", cfg.p13b), 0, N_SLOTS - 1);
  cfg.p13c = constrain(prefs->getInt("p13c", cfg.p13c), 0, N_SLOTS - 1);
  cfg.p13d = constrain(prefs->getInt("p13d", cfg.p13d), 0, N_SLOTS - 1);
  for (int i = 0; i < N_CALS; i++) cfg.*(CALS[i].f) = constrain(prefs->getInt(CALS[i].k, CALS[i].def), CALS[i].mn, CALS[i].mx);
  calFix();
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
  prefs->putInt("p13a", cfg.p13a);
  prefs->putInt("p13spd", cfg.p13spd);
  prefs->putInt("anim", cfg.anim);
  prefs->putInt("picLay", cfg.picLay);
  prefs->putInt("needle", cfg.needle);
  prefs->putInt("pgMask", cfg.pgMask);
  prefs->putInt("p13b", cfg.p13b);
  prefs->putInt("p13c", cfg.p13c);
  prefs->putInt("p13d", cfg.p13d);
  for (int i = 0; i < N_CALS; i++) prefs->putInt(CALS[i].k, cfg.*(CALS[i].f));
}

// ------------------------------------------------------------------ JSON
inline String toJson() {
  String s;
  s.reserve(560);
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
  s += ",\"pgMask\":";
  s += cfg.pgMask;
  s += ",\"anim\":";
  s += cfg.anim;
  s += ",\"picLay\":";
  s += cfg.picLay;
  s += ",\"needle\":";
  s += cfg.needle;
  s += ",\"p13spd\":";
  s += cfg.p13spd;
  s += ",\"p13a\":";
  s += cfg.p13a;
  s += ",\"p13b\":";
  s += cfg.p13b;
  s += ",\"p13c\":";
  s += cfg.p13c;
  s += ",\"p13d\":";
  s += cfg.p13d;
  for (int i = 0; i < N_CALS; i++) { s += ",\""; s += CALS[i].k; s += "\":"; s += cfg.*(CALS[i].f); }
  if (rawTps) { s += ",\"tpsRaw\":"; s += String(rawTps(), 1); }
  s += "},\"slots\":[";
  for (int i = 0; i < N_SLOTS; i++) {
    if (i) s += ",";
    s += "\"";
    s += SLOT_NAMES[i];
    s += "\"";
  }
  s += "],\"pages\":[";
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
  else if (k == "p13spd") cfg.p13spd = iv ? 1 : 0;
  else if (k == "anim") cfg.anim = iv ? 1 : 0;
  else if (k == "picLay") cfg.picLay = iv & 7;
  else if (k == "lay14" || k == "lay15" || k == "lay16") { int b = 1 << (k[4] - '4'); cfg.picLay = iv ? (cfg.picLay | b) : (cfg.picLay & ~b); }
  else if (k == "needle") cfg.needle = constrain(iv, 0, 5);
  else if (k == "pgMask") {
    int m = iv & ((1 << pageCount) - 1);
    if (m) {
      cfg.pgMask = m;
      for (int t = 0; t < pageCount && !((m >> cfg.page) & 1); t++) cfg.page = (cfg.page + 1) % pageCount;   // หน้าที่โชว์อยู่ถูกปิด → ไปหน้าถัดไปที่เปิด
    }
  }
  else if (k == "p13a") cfg.p13a = constrain(iv, 0, N_SLOTS - 1);
  else if (k == "p13b") cfg.p13b = constrain(iv, 0, N_SLOTS - 1);
  else if (k == "p13c") cfg.p13c = constrain(iv, 0, N_SLOTS - 1);
  else if (k == "p13d") cfg.p13d = constrain(iv, 0, N_SLOTS - 1);
  else if (k == "tpsSet") {                       // 0 = ตั้ง TPS ตอนนี้เป็น 0% · 1 = เป็น 100%
    if (!rawTps) return false;
    int r = constrain((int)lroundf(rawTps()), 0, 100);
    if (iv) cfg.tps1 = constrain(r, 30, 100); else cfg.tps0 = constrain(r, 0, 60);
    if (iv) { if (cfg.tps0 > cfg.tps1 - 10) cfg.tps0 = max(0, cfg.tps1 - 10); } else calFix();
  }
  else if (k == "calReset") calReset();
  else if ([&] { for (int i = 0; i < N_CALS; i++) if (k == CALS[i].k) { cfg.*(CALS[i].f) = constrain(iv, CALS[i].mn, CALS[i].mx); calFix(); return true; } return false; }()) {}
  else if (k.length() == 2 && k[0] == 'g' && k[1] >= '0' && k[1] <= '5') {
    if (v >= 5.0f && v <= 400.0f) cfg.gears[k[1] - '0'] = v;
  } else return false;
  return true;
}

static const char* const KEYS[] = {"maxRpm", "tempAlert", "bri", "briAuto", "flip", "page",
                                   "autoCycle", "gearCount", "g0", "g1", "g2", "g3", "g4", "g5", "p13a", "p13b", "p13c", "p13d", "p13spd", "anim", "needle", "picLay", "pgMask", "spdG", "rpmG", "afrG", "ectO", "iatO", "vOfs", "tps0", "tps1", "tpsSet", "calReset"};
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
inline void saveLater() { saveAt = millis() + 2500; if (!saveAt) saveAt = 1; }
inline void changed(int oldPage) {
  saveLater();
  if (cfg.page != oldPage && onPage) onPage(cfg.page);
  if (onApply) onApply();
  lastCycle = millis();
}

// เปลี่ยนหน้าจากที่อื่น (ปุ่ม/แอป/เว็บ) แล้วจำไว้
// หน้าเปิดใช้อยู่ไหม / หาหน้าถัดไปที่เปิดอยู่ (dir = +1 / -1)
inline bool pageOn(int p) { return (cfg.pgMask >> p) & 1; }
inline int stepPage(int from, int dir) {
  for (int k = 1; k <= pageCount; k++) {
    int p = (((from + dir * k) % pageCount) + pageCount) % pageCount;
    if (pageOn(p)) return p;
  }
  return from;
}
inline void setPage(int p) {
  if (pageCount <= 0) return;
  p = ((p % pageCount) + pageCount) % pageCount;
  int old = cfg.page;
  cfg.page = p;
  if (p != old) saveLater();
  if (p != old && onPage) onPage(p);
  lastCycle = millis();
}

// ------------------------------------------------------------------ คำสั่งจากแอป (TCP)
inline bool handleLine(const String& line) {
  if (line.indexOf("\"get_cfg\"") >= 0) {
    didilink::reply(toJson());
    return true;
  }
  if (line.indexOf("\"set_cfg\"") >= 0) {
    int old = cfg.page;
    for (int i = 0; i < N_KEYS; i++) {
      float v;
      if (numField(line, KEYS[i], v)) setKV(KEYS[i], v);
    }
    changed(old);
    didilink::reply(toJson());
    return true;
  }
  if (line.indexOf("\"page\"") >= 0 && line.indexOf("\"cmd\"") >= 0) {
    float v;
    if (numField(line, "v", v)) setPage((int)v);
    didilink::reply(toJson());
    return true;
  }
  if (line.indexOf("\"peaks_reset\"") >= 0) {
    if (onPeaksReset) onPeaksReset();
    didilink::reply("{\"ok\":true}");
    return true;
  }
  if (line.indexOf("\"trip_reset\"") >= 0) {
    if (onTripReset) onTripReset();
    didilink::reply("{\"ok\":true}");
    return true;
  }
  return false;   // ไม่ใช่คำสั่งของเรา (เช่น dtc_read) ให้ DidissnapLink จัดการต่อ
}

// ------------------------------------------------------------------ หน้าเว็บ
static const char PAGE_HTML[] = R"HTML(<!DOCTYPE html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Didissnap ตั้งค่าจอ</title>
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
.pic{display:flex;align-items:center;gap:8px;padding:6px 0;border-top:1px solid #1c1c1c}
.pic img{width:96px;height:54px;object-fit:cover;border-radius:4px;background:#222}
.pic .t{flex:1;min-width:0;font-size:12px;color:#aaa;word-break:break-all}
.pic button{padding:6px 10px;margin:0}
.bar{height:6px;background:#222;border-radius:3px;overflow:hidden}.bar i{display:block;height:100%;background:#FF7A00}
.hint{color:#888;font-size:12px;padding:4px 0}
</style></head><body>
<header>Didissnap · ตั้งค่าจอ ESP32</header>
<div class="live" id="live"></div>
<section><h3>หน้าจอ</h3>
<div class="row"><span>หน้าที่แสดง</span><select id="page"></select></div>
<div class="row"><span>หน้าที่ใช้ (ปิด = กดปุ่มข้ามไป)</span></div><div id="pgs" style="display:flex;flex-wrap:wrap;gap:6px 14px;margin:0 0 8px"></div>
<div class="row"><span>สลับหน้าเองทุก (วินาที, 0 = ปิด)</span><input type="number" id="autoCycle" min="0" max="120"></div>
<div class="row"><span>ความสว่าง <b id="briV"></b></span><input type="range" id="bri" min="10" max="100" step="10"></div>
<div class="row"><span>ปรับความสว่างตามแสงอัตโนมัติ</span><input type="checkbox" class="sw" id="briAuto"></div>
<div class="row"><span>กลับจอ 180°</span><input type="checkbox" class="sw" id="flip"></div>
<div class="row" id="needleR" style="display:none"><span>แบบเข็ม</span><select id="needle"><option value="0">CLASSIC</option><option value="1">TAPER</option><option value="2">GLOW</option><option value="3">SPORT</option><option value="4">ARROW</option><option value="5">BLADE 3D</option></select></div>
<div class="row" id="animR" style="display:none"><span>อนิเมชันเกจ (กวาดเข็ม สปริง ค้างค่าสูงสุด ไฟ shift)</span><input type="checkbox" class="sw" id="anim"></div>
<div class="row p13r" style="display:none"><span>หน้า 13 โชว์ความเร็ว</span><input type="checkbox" class="sw" id="p13spd"></div>
<div class="row p13r" style="display:none"><span>หน้า 13 แถวล่าง ช่อง 1</span><select id="p13a"></select></div>
<div class="row p13r" style="display:none"><span>หน้า 13 แถวล่าง ช่อง 2</span><select id="p13b"></select></div>
<div class="row p13r" style="display:none"><span>หน้า 13 แถวล่าง ช่อง 3</span><select id="p13c"></select></div>
<div class="row p13r" style="display:none"><span>หน้า 13 แถวล่าง ช่อง 4</span><select id="p13d"></select></div>
<div class="row lay" style="display:none"><span>หน้า 14 แบบ</span><select id="lay14"><option value="0">แบบเดิม (แถบบน)</option><option value="1">แบบหน้า 13</option></select></div>
<div class="row lay" style="display:none"><span>หน้า 15 แบบ</span><select id="lay15"><option value="0">แบบเดิม (แถบบน)</option><option value="1">แบบหน้า 13</option></select></div>
<div class="row lay" style="display:none"><span>หน้า 16 แบบ</span><select id="lay16"><option value="0">แบบเดิม (แถบบน)</option><option value="1">แบบหน้า 13</option></select></div></section>
<section><h3>รอบ / ความร้อน</h3>
<div class="row"><span>Redline (rpm)</span><input type="number" id="maxRpm" min="2000" max="12000" step="250"></div>
<div class="row"><span>เตือนน้ำที่ (°C)</span><input type="number" id="tempAlert" min="70" max="130"></div></section>
<section id="calSec" style="display:none"><h3>ปรับค่าให้ตรง</h3>
<div class="row"><span>ความเร็ว เกน (%)</span><input type="number" id="spdG" min="80" max="130"></div>
<div class="row"><span>รอบ เกน (%)</span><input type="number" id="rpmG" min="80" max="120"></div>
<div class="row"><span>AFR เกน (%)</span><input type="number" id="afrG" min="80" max="120"></div>
<div class="row"><span>น้ำ บวก/ลบ (°C)</span><input type="number" id="ectO" min="-15" max="15"></div>
<div class="row"><span>อากาศเข้า บวก/ลบ (°C)</span><input type="number" id="iatO" min="-15" max="15"></div>
<div class="row"><span>แบต บวก/ลบ (V)</span><input type="number" id="vOfsV" min="-2" max="2" step="0.1"></div>
<div class="row"><span>TPS จุด 0% (ค่าดิบ %)</span><input type="number" id="tps0" min="0" max="60"></div>
<div class="row"><span>TPS จุด 100% (ค่าดิบ %)</span><input type="number" id="tps1" min="30" max="100"></div>
<div class="hint" id="tpsRaw"></div>
<button onclick="set('tpsSet',0)">ตั้ง TPS ตอนนี้ = 0%</button><button onclick="set('tpsSet',1)">ตั้ง TPS ตอนนี้ = 100%</button>
<button class="o" onclick="if(confirm('คืนค่าปรับทั้งหมด?'))set('calReset',1)">คืนค่าเดิม</button></section>
<section><h3>เกียร์ (rpm ต่อ 1 km/h)</h3>
<div class="row"><span>จำนวนเกียร์</span><input type="number" id="gearCount" min="3" max="6"></div>
<div id="gears"></div></section>
<section id="picSec" style="display:none"><h3>รูปบนจอ</h3>
<div class="hint" id="picFree"></div><div class="bar"><i id="picBar"></i></div>
<div class="row"><input type="file" id="pf" accept=".jpg,.jpeg,.gif,image/jpeg,image/gif" style="flex:1;min-width:0;color:#aaa;font-size:13px"><button onclick="up()">อัปโหลด</button></div>
<div class="hint">ควรเป็นขนาด 480×272 · ใช้แอป OBD2_Didissnap จะครอป/ย่อให้อัตโนมัติ</div>
<div id="slots"></div>
<div class="row"><span>หน้ารูปตามรอบ: แบ่งช่วงรอบอัตโนมัติ</span><input type="checkbox" class="sw" id="pAuto"></div>
<div id="pics"></div></section>
<section><h3>DTC / รีเซ็ต</h3>
<button onclick="dtc()">อ่าน DTC</button><button class="o" onclick="dtcClear()">ลบ DTC</button>
<div class="codes" id="codes"></div>
<button class="o" onclick="act('peaks_reset','ล้างค่าสูงสุดแล้ว')">ล้างค่าสูงสุด</button>
<button class="o" onclick="act('trip_reset','รีเซ็ตทริปแล้ว')">รีเซ็ตทริป</button></section>
<div id="msg"></div>
<script>
const $=i=>document.getElementById(i);let C={};
function msg(t){const m=$('msg');m.textContent=t;m.style.opacity=1;clearTimeout(msg.t);msg.t=setTimeout(()=>m.style.opacity=0,1800)}
function fill(j){C=j.cfg;if(j.slots){['p13a','p13b','p13c','p13d'].forEach(k=>{const e=$(k);e.innerHTML=j.slots.map((n,i)=>`<option value="${i}">${n}</option>`).join('');e.value=C[k];});document.querySelectorAll('.p13r').forEach(r=>r.style.display='');}const p=$('page');p.innerHTML=j.pages.map((n,i)=>`<option value="${i}">${i+1}. ${n}</option>`).join('');p.value=C.page;
if(C.pgMask!==undefined){const g=$('pgs');g.innerHTML=j.pages.map((n,i)=>`<label style="font-size:13px"><input type="checkbox" data-i="${i}" ${(C.pgMask>>i)&1?'checked':''}> ${i+1}. ${n}</label>`).join('');g.querySelectorAll('input').forEach(e=>e.onchange=()=>{let m=0;g.querySelectorAll('input').forEach(x=>{if(x.checked)m|=1<<x.dataset.i});if(!m){e.checked=true;return;}set('pgMask',m);});}
['maxRpm','tempAlert','autoCycle','gearCount','bri'].forEach(k=>$(k).value=C[k]);$('briV').textContent=C.bri+'%';
if(C.spdG!==undefined){$('calSec').style.display='';['spdG','rpmG','afrG','ectO','iatO','tps0','tps1'].forEach(k=>$(k).value=C[k]);$('vOfsV').value=(C.vOfs/10).toFixed(1);$('tpsRaw').textContent=C.tpsRaw!==undefined?'TPS ดิบตอนนี้ '+C.tpsRaw+' %':'';}
if(C.picLay!==undefined){document.querySelectorAll('.lay').forEach(r=>r.style.display='');[14,15,16].forEach(n=>$('lay'+n).value=(C.picLay>>(n-14))&1);}
if(C.needle!==undefined){$('needleR').style.display='';$('needle').value=C.needle;}
if(C.anim!==undefined){$('animR').style.display='';$('anim').checked=!!C.anim;}
$('briAuto').checked=!!C.briAuto;$('flip').checked=!!C.flip;$('p13spd').checked=C.p13spd!==0;
let g='';for(let i=0;i<C.gearCount;i++)g+=`<div class="row"><span>เกียร์ ${i+1}</span><input type="number" step="0.1" id="g${i}" value="${C['g'+i]}"></div>`;$('gears').innerHTML=g;
for(let i=0;i<C.gearCount;i++)$('g'+i).onchange=e=>set('g'+i,e.target.value)}
async function load(){fill(await (await fetch('/api/cfg')).json())}
async function set(k,v){const r=await fetch('/api/set?'+k+'='+encodeURIComponent(v));fill(await r.json());msg('บันทึกแล้ว')}
['maxRpm','tempAlert','autoCycle','gearCount','page','p13a','p13b','p13c','p13d','spdG','rpmG','afrG','ectO','iatO','tps0','tps1'].forEach(k=>$(k).onchange=e=>set(k,e.target.value));$('vOfsV').onchange=e=>set('vOfs',Math.round(e.target.value*10));
$('bri').oninput=e=>$('briV').textContent=e.target.value+'%';$('bri').onchange=e=>set('bri',e.target.value);
$('briAuto').onchange=e=>set('briAuto',e.target.checked?1:0);$('flip').onchange=e=>set('flip',e.target.checked?1:0);$('p13spd').onchange=e=>set('p13spd',e.target.checked?1:0);$('anim').onchange=e=>set('anim',e.target.checked?1:0);$('needle').onchange=e=>set('needle',e.target.value);[14,15,16].forEach(n=>$('lay'+n).onchange=e=>set('lay'+n,e.target.value));
async function dtc(){$('codes').textContent='กำลังอ่าน...';const j=await (await fetch('/api/dtc')).json();$('codes').innerHTML=j.dtc.length?j.dtc.map(c=>`<div>⚠ ${c}</div>`).join(''):'<div style="color:#00E05A">ไม่พบรหัส DTC ✓</div>'}
async function dtcClear(){if(!confirm('ลบรหัส DTC และดับไฟ Check Engine?'))return;const j=await (await fetch('/api/dtc_clear')).json();msg(j.cleared?'ลบแล้ว':'ECU ไม่ยืนยัน');if(j.cleared)$('codes').innerHTML=''}
async function act(a,t){await fetch('/api/'+a);msg(t)}
const L=[['rpm','รอบ',''],['spd','km/h',''],['ect','น้ำ','°C'],['iat','IAT','°C'],['volt','แบต','V'],['load','Load','%'],['tps','TPS','%'],['map','MAP','kPa'],['fuel','น้ำมัน','%']];
async function live(){try{const j=await (await fetch('/api/live')).json();$('live').innerHTML=L.map(([k,n,u])=>`<div><small>${n}</small><b>${j[k]!==undefined?j[k]:'--'}${u}</b></div>`).join('')}catch(e){}setTimeout(live,700)}
let P=null;
function fmtB(b){return b>1048576?(b/1048576).toFixed(1)+' MB':Math.round(b/1024)+' KB'}
function rp(j){if(!j||!j.ok){if(j&&j.err)msg('ผิดพลาด: '+j.err);return}P=j;$('picSec').style.display='';
$('picFree').textContent='ใช้ไป '+fmtB(j.used)+' / '+fmtB(j.total)+' · '+j.pics.length+' รูป';$('picBar').style.width=(j.used/j.total*100)+'%';
const opt=v=>'<option value="">รูปเดิมในบอร์ด</option>'+j.pics.map(p=>`<option ${p.n==v?'selected':''}>${p.n}</option>`).join('');
$('slots').innerHTML=[9,10,11].map((pg,i)=>`<div class="row"><span>หน้า ${pg}</span><select onchange="pa('pic_slot?slot=${i}&name='+this.value)">${opt(j.slots[i])}</select></div>`).join('');
$('pAuto').checked=!!j.auto;
$('pics').innerHTML=j.pics.map((p,i)=>`<div class="pic"><img src="/pics/${p.n}" loading="lazy"><div class="t">${p.n}<br>${fmtB(p.s)}${j.auto?'':`<br>เริ่มที่ <input type="number" style="width:70px;padding:4px;font-size:13px" value="${p.rpm}" onchange="pa('pic_rpm?name=${p.n}&rpm='+this.value)"> rpm`}</div>
<input type="checkbox" class="sw" title="ใช้ในหน้ารูปตามรอบ" ${p.on?'checked':''} onchange="pa('pic_rpm?name=${p.n}&on='+(this.checked?1:0))">
${i?`<button class="o" onclick="pa('pic_move?name=${p.n}&dir=-1')">↑</button>`:''}<button class="o" onclick="if(confirm('ลบ ${p.n}?'))pa('pic_del?name=${p.n}')">ลบ</button></div>`).join('')}
async function pa(q){try{rp(await (await fetch('/api/'+q)).json())}catch(e){}}
$('pAuto').onchange=e=>pa('pic_auto?v='+(e.target.checked?1:0));
async function up(){const f=$('pf').files[0];if(!f)return msg('เลือกไฟล์ก่อน');const gif=/gif$/i.test(f.type)||/\.gif$/i.test(f.name);
let n=f.name.toLowerCase().replace(/\.[^.]*$/,'').replace(/[^a-z0-9_-]/g,'_').slice(0,20)||'pic';n+=gif?'.gif':'.jpg';
const fd=new FormData();fd.append('f',f,n);msg('กำลังอัปโหลด...');
try{const r=await fetch('/api/pic_up?name='+n,{method:'POST',body:fd});const j=await r.json();rp(j);if(j.ok){msg('อัปโหลดแล้ว');$('pf').value=''}}catch(e){msg('อัปโหลดไม่สำเร็จ')}}
pa('pics');load();live();
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
  web.on("/api/live", []() { web.send(200, "application/json", didilink::lastJson); });
  web.on("/api/dtc", []() {
    String arr = didilink::onDtcRead ? didilink::onDtcRead() : String("[]");
    web.send(200, "application/json", "{\"dtc\":" + arr + "}");
  });
  web.on("/api/dtc_clear", []() {
    bool ok = didilink::onDtcClear ? didilink::onDtcClear() : false;
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

// เรียกใน setup() หลัง didilink::begin()
inline void begin(Preferences& p, const char* const* names, int count) {
  prefs = &p;
  pageNames = names;
  pageCount = count > 0 ? count : 1;
  load();
  didilink::onCommand = handleLine;
  setupWeb();
  lastCycle = millis();
}

// บันทึกค่าที่ค้างไว้ (เรียกได้ทุกรอบ แม้ปิด WiFi)
inline void tick() {
  if (saveAt && (int32_t)(millis() - saveAt) >= 0) { saveAt = 0; save(); }
}

// เรียกทุกรอบใน loop()
inline void loop() {
  web.handleClient();
  tick();
  if (cfg.autoCycle > 0 && millis() - lastCycle >= (uint32_t)cfg.autoCycle * 1000UL) {
    int p = stepPage(cfg.page, 1);   // ข้ามหน้าที่ปิดไว้
    cfg.page = p;                 // สลับอัตโนมัติไม่บันทึกลงแฟลชทุกครั้ง (ถนอมแฟลช)
    if (onPage) onPage(p);
    lastCycle = millis();
  }
}

}  // namespace didicfg
