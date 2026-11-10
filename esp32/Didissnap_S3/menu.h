// menu.h — เมนูบนจอ ใช้ปุ่ม 2 ปุ่ม (เหมือนในพรีวิว)
//  หน้าปกติ: ปุ่ม1 สั้น = หน้าถัดไป · ปุ่ม2 สั้น = หน้าก่อน · ปุ่ม1 ค้าง = เปิดเมนู · ปุ่ม2 ค้าง = ARM/STOP (หน้า Record)
//  ในเมนู : ปุ่ม1 = ลง · ปุ่ม2 = ขึ้น · ปุ่ม2 ค้าง = เลือก/แก้ค่า · ปุ่ม1 ค้าง = ออก
//  แก้ค่า : ปุ่ม1 = เพิ่ม · ปุ่ม2 = ลด · ปุ่ม2 ค้าง = ตกลง
#pragma once
#include "gfx.h"
#include "pages.h"
#include "DidissnapConfig.h"

namespace menu {
using namespace gfx;

struct Item { const char* label; const char* key; int mn, mx, st; };
static const Item ITEMS[] = {
    {"PAGE", "page", 0, pages::N - 1, 1},
    {"BRIGHTNESS", "bri", 10, 100, 10},
    {"AUTO BRIGHT", "briAuto", 0, 1, 1},
    {"REDLINE", "maxRpm", 2000, 12000, 250},
    {"TEMP ALERT", "tempAlert", 70, 130, 1},
    {"AUTO CYCLE", "autoCycle", 0, 120, 5},
    {"FLIP 180", "flip", 0, 1, 1},
    {"GEARS", "gearCount", 3, 6, 1},
    {"REFRESH", "lcdMode", 0, 3, 1},
    {"PAGE 13 VALUES", nullptr, 0, 0, 0},
    {"OBD BLE", "obdBle", 0, 1, 1},
    {"PERF INFO", "perf", 0, 1, 1},
    {"ADVANCED", nullptr, 0, 0, 0},
    {"PEAK RESET", nullptr, 0, 0, 0},
    {"TRIP RESET", nullptr, 0, 0, 0},
    {"WIFI", nullptr, 0, 0, 0},
    {"EXIT", nullptr, 0, 0, 0},
};
static const int N = sizeof(ITEMS) / sizeof(ITEMS[0]);
// เมนูย่อย ADVANCED: ค่าสำหรับทดสอบ/ปรับจูนจอ (ไม่ต้องยุ่งตอนใช้งานปกติ)
static const Item ADV[] = {
    {"DRAW CORES", "cores", 1, 2, 1},
    {"COPY SPEED", "copySpd", 10, 80, 10},
    {"LCD BOUNCE", "bounce", 0, 1, 1},
    {"BAND SKIP", "bandSkip", 0, 1, 1},
    {"GIF TEST", "gifTest", 0, 4, 1},
    {"OBD FORGET", nullptr, 0, 0, 0},
    {"BACK", nullptr, 0, 0, 0},
};
static const int NA = sizeof(ADV) / sizeof(ADV[0]);
// เมนูย่อย PAGE 13 VALUES: ค่า 4 ช่องแถวล่างของหน้า 13
static const Item P13[] = {
    {"SLOT 1", "p13a", 0, 0, 1},
    {"SLOT 2", "p13b", 0, 0, 1},
    {"SLOT 3", "p13c", 0, 0, 1},
    {"SLOT 4", "p13d", 0, 0, 1},
    {"BACK", nullptr, 0, 0, 0},
};
static const int NP = sizeof(P13) / sizeof(P13[0]);
static int sub = 0;           // 0 = เมนูหลัก, 1 = ADVANCED, 2 = PAGE 13 VALUES
static bool adv = false;      // (เก็บไว้ให้โค้ดเดิม) = อยู่ในเมนูย่อยใดก็ได้
static int mainSel = 0;       // ตำแหน่งในเมนูหลักตอนเข้าเมนูย่อย
static const char* const SUB_TITLE[] = {"MENU", "MENU > ADVANCED", "MENU > PAGE 13 VALUES"};
static inline const Item* L() { return sub == 1 ? ADV : (sub == 2 ? P13 : ITEMS); }
static inline int LN() { return sub == 1 ? NA : (sub == 2 ? NP : N); }
static inline void enterSub(int k, int cur) { mainSel = cur; sub = k; adv = k != 0; }
static bool open = false, edit = false;
static int sel = 0;
static void (*onPeaks)() = nullptr;
static void (*onTrip)() = nullptr;
static void (*onHold2)() = nullptr;
// อัตรารีเฟรชจอ: 0 = 44 Hz · 1 = 30 Hz · 2 = 60 Hz ล็อก 30 fps · 3 = 60 Hz
static int lcdMode = 0;
static const char* const LCD_MODE_NAME[] = {"44 Hz", "30 Hz (30 fps)", "60 Hz lock 30 fps", "60 Hz"};
static void (*onLcdMode)(int) = nullptr;
static int bounceMode = 1;                 // 1 = จอใช้ bounce buffer (ค่าเริ่มต้น — ทดสอบแล้วหายวูบ) มีผลหลังรีสตาร์ท
static void (*onBounce)(int) = nullptr;
// ทดสอบความเร็ว: วาดกี่คอร์ / อ่านตัวอักษรจาก PSRAM หรือแฟลช
static int drawCores = 1, fontMem = 0, perfInfo = 0;   // perfInfo = ตัวเลข fps มุมล่างซ้าย
static void (*onPerfOpt)() = nullptr;
static int obdMode = 1;                        // 1 = ต่อตัวอ่าน OBD แบบ BLE (มีผลหลังรีสตาร์ท)
static void (*onObd)(int) = nullptr;
static void (*onObdForget)() = nullptr;
static const char* (*obdStatus)() = nullptr;   // ปุ่ม 2 ค้างตอนอยู่หน้าปกติ (หน้า Record = ARM/STOP)

static char toastMsg[32] = "";
static uint32_t toastUntil = 0;
inline void toast(const char* m) { strlcpy(toastMsg, m, sizeof(toastMsg)); toastUntil = millis() + 1400; }

inline int value(const char* k) {
  const auto& c = didicfg::cfg;
  if (!strcmp(k, "page")) return c.page;
  if (!strcmp(k, "bri")) return c.bri;
  if (!strcmp(k, "briAuto")) return c.briAuto;
  if (!strcmp(k, "maxRpm")) return c.maxRpm;
  if (!strcmp(k, "tempAlert")) return c.tempAlert;
  if (!strcmp(k, "autoCycle")) return c.autoCycle;
  if (!strcmp(k, "flip")) return c.flip;
  if (!strcmp(k, "gearCount")) return c.gearCount;
  if (!strcmp(k, "p13a")) return c.p13a;
  if (!strcmp(k, "p13b")) return c.p13b;
  if (!strcmp(k, "p13c")) return c.p13c;
  if (!strcmp(k, "p13d")) return c.p13d;
  if (!strcmp(k, "lcdMode")) return lcdMode;
  if (!strcmp(k, "cores")) return drawCores;
  if (!strcmp(k, "fontMem")) return fontMem;
  if (!strcmp(k, "perf")) return perfInfo;
  if (!strcmp(k, "gifTest")) return bg::gifTest;
  if (!strcmp(k, "copySpd")) return (int)lcd::copyMBps;
  if (!strcmp(k, "bounce")) return bounceMode;
  if (!strcmp(k, "bandSkip")) return lcd::bandSkip ? 1 : 0;
  if (!strcmp(k, "obdBle")) return obdMode;
  return 0;
}
inline void fmt(const Item& it, char* b, size_t n) {
  if (!it.key) {
    if (!strcmp(it.label, "WIFI")) snprintf(b, n, "Didissnap  192.168.4.1");
    else if (!strcmp(it.label, "BACK")) snprintf(b, n, "<");
    else snprintf(b, n, ">");
    return;
  }
  int v = value(it.key);
  if (!strcmp(it.key, "page")) snprintf(b, n, "%d %s", v + 1, pages::NAMES[v]);
  else if (!strcmp(it.key, "bri")) snprintf(b, n, "%d%%", v);
  else if (!strcmp(it.key, "maxRpm")) snprintf(b, n, "%d rpm", v);
  else if (!strcmp(it.key, "tempAlert")) snprintf(b, n, "%d C", v);
  else if (!strcmp(it.key, "autoCycle")) { if (v) snprintf(b, n, "%d s", v); else snprintf(b, n, "OFF"); }
  else if (!strncmp(it.key, "p13", 3)) snprintf(b, n, "%s", didicfg::SLOT_NAMES[v % didicfg::N_SLOTS]);
  else if (!strcmp(it.key, "obdBle")) snprintf(b, n, "%s  %s", v ? "ON" : "OFF", obdStatus ? obdStatus() : "");
  else if (!strcmp(it.key, "copySpd")) snprintf(b, n, "%d MB/s", v);
  else if (!strcmp(it.key, "gifTest")) { static const char* const GT[] = {"NORMAL", "DECODE ONLY", "NO DECODE", "DECODE NO WRITE", "CPU LOAD ONLY"}; snprintf(b, n, "%s", GT[v % 5]); }
  else if (!strcmp(it.key, "fontMem")) snprintf(b, n, v ? "FLASH" : "PSRAM");
  else if (!strcmp(it.key, "lcdMode")) snprintf(b, n, "%s", LCD_MODE_NAME[v & 3]);
  else if (!strcmp(it.key, "briAuto") || !strcmp(it.key, "flip") || !strcmp(it.key, "perf") || !strcmp(it.key, "bounce") || !strcmp(it.key, "bandSkip")) snprintf(b, n, v ? "ON" : "OFF");
  else snprintf(b, n, "%d", v);
}
inline void set(const char* k, int v) {
  if (!strcmp(k, "lcdMode")) { lcdMode = v & 3; if (onLcdMode) onLcdMode(lcdMode); return; }
  if (!strcmp(k, "cores")) { drawCores = v; if (onPerfOpt) onPerfOpt(); return; }
  if (!strcmp(k, "fontMem")) { fontMem = v; if (onPerfOpt) onPerfOpt(); return; }
  if (!strcmp(k, "gifTest")) { bg::gifTest = v; return; }
  if (!strcmp(k, "bandSkip")) { lcd::bandSkip = v != 0; return; }
  if (!strcmp(k, "obdBle")) { obdMode = v & 1; if (onObd) onObd(obdMode); return; }
  if (!strcmp(k, "bounce")) { bounceMode = v; if (onBounce) onBounce(v); return; }
  if (!strcmp(k, "copySpd")) { lcd::copyMBps = v; if (onPerfOpt) onPerfOpt(); return; }
  if (!strcmp(k, "perf")) { perfInfo = v; if (onPerfOpt) onPerfOpt(); return; }
  int old = didicfg::cfg.page;
  didicfg::setKV(String(k), (float)v);
  didicfg::changed(old);
}

inline void draw() {
  using pages::ORG;
  clear(rgb(0x0A0A0A));
  fill(0, 0, W, 28, rgb(0x1A1512));
  text(SUB_TITLE[sub], 12, 20, 16, ORG, LEFT, 900);
  const Item* IT = L();
  const int N = LN();
  text("Didissnap", 468, 20, 12, rgb(0x999999), RIGHT, 600);
  const int rows = 9;
  int top = max(0, min(sel - 4, N - rows));
  bool bl = (nowMs() / 250) % 2 == 0;
  for (int r = 0; r < rows; r++) {
    int j = top + r;
    if (j >= N) break;
    int y = 32 + r * 23;
    bool s = j == sel;
    if (s) rrect(6, y, 468, 21, 4, edit ? rgb(0x5A2E00) : rgb(0x2A2320), edit ? ORG : rgb(0x8A4A10), 1.5f);
    text(IT[j].label, 16, y + 16, 14, s ? rgb(0xFFB37A) : rgb(0xDDDDDD), LEFT, 700);
    char v[40];
    fmt(IT[j], v, sizeof(v));
    text(v, 464, y + 16, 13, s && edit ? (bl ? 0xFFFF : ORG) : rgb(0xAAAAAA), RIGHT, 600);
  }
  fill(0, H - 22, W, 22, rgb(0x1A1512));
  text(edit ? "BTN1 = +    BTN2 = -    HOLD BTN2 = OK" : (adv ? "BTN1 = DOWN   BTN2 = UP   HOLD BTN2 = SELECT   HOLD BTN1 = BACK" : "BTN1 = DOWN   BTN2 = UP   HOLD BTN2 = SELECT   HOLD BTN1 = EXIT"),
       W / 2, H - 7, 11, rgb(0xBBBBBB), CENTER, 600);
}

inline void drawToast() {
  if (nowMs() >= toastUntil) return;
  rrect(W / 2 - 90, H / 2 - 18, 180, 36, 8, rgb(0x101010), pages::ORG, 1.5f);
  text(toastMsg, W / 2, H / 2 + 6, 16, pages::ORG, CENTER, 800);
}

// ปุ่ม: b = 0 (ปุ่ม 1) / 1 (ปุ่ม 2)
inline void button(int b, bool lng) {
  if (!open) {
    if (b == 0 && lng) { open = true; sel = 0; edit = false; enterSub(0, 0); return; }
    if (lng) { if (onHold2) onHold2(); return; }
    int p = didicfg::cfg.page + (b == 0 ? 1 : -1);
    didicfg::setPage(((p % pages::N) + pages::N) % pages::N);
    return;
  }
  const Item& it = L()[sel];
  const int N = LN();
  if (edit) {
    if (b == 1 && lng) { edit = false; return; }
    if (lng) return;
    int v = value(it.key) + (b == 0 ? it.st : -it.st);
    if (!strcmp(it.key, "page")) v = (v + pages::N) % pages::N;
    else if (!strncmp(it.key, "p13", 3)) v = (v + didicfg::N_SLOTS) % didicfg::N_SLOTS;   // วนรอบ
    else v = constrain(v, it.mn, it.mx);
    set(it.key, v);
    return;
  }
  if (b == 0 && lng) {   // ปุ่ม 1 ค้าง: ในเมนูย่อย = กลับเมนูหลัก, เมนูหลัก = ออก
    if (sub) { int m = mainSel; enterSub(0, 0); sel = m; } else open = false;
    return;
  }
  if (b == 0) { sel = (sel + 1) % N; return; }
  if (!lng) { sel = (sel + N - 1) % N; return; }
  // ปุ่ม 2 ค้าง = เลือก
  if (it.key) edit = true;
  else if (!strcmp(it.label, "PEAK RESET")) { if (onPeaks) onPeaks(); toast("PEAK RESET"); }
  else if (!strcmp(it.label, "TRIP RESET")) { if (onTrip) onTrip(); toast("TRIP RESET"); }
  else if (!strcmp(it.label, "OBD FORGET")) { if (onObdForget) onObdForget(); }
  else if (!strcmp(it.label, "ADVANCED")) { enterSub(1, sel); sel = 0; }
  else if (!strcmp(it.label, "PAGE 13 VALUES")) { enterSub(2, sel); sel = 0; }
  else if (!strcmp(it.label, "BACK")) { int m = mainSel; enterSub(0, 0); sel = m; }
  else if (!strcmp(it.label, "EXIT")) open = false;
}

}  // namespace menu
