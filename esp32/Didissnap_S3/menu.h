// menu.h — เมนูบนจอ ใช้ปุ่ม 2 ปุ่ม (เหมือนในพรีวิว)
//  หน้าปกติ: ปุ่ม1 สั้น = หน้าถัดไป · ปุ่ม2 สั้น = หน้าก่อน · ปุ่ม1 ค้าง = เปิดเมนู · ปุ่ม2 ค้าง = ARM/STOP (หน้า Record)
//  หน้าแรกเมนู (ช่องไอคอน 6 หมวด): ปุ่ม1 = ช่องถัดไป · ปุ่ม2 = ช่องก่อน · ปุ่ม2 ค้าง = เข้าหมวด · ปุ่ม1 ค้าง = ออก
//  ในหมวด : ปุ่ม1 = ลง · ปุ่ม2 = ขึ้น · ปุ่ม2 ค้าง = เลือก/แก้ค่า · ปุ่ม1 ค้าง = กลับหน้าช่องไอคอน
//  แก้ค่า : ปุ่ม1 = เพิ่ม · ปุ่ม2 = ลด · ปุ่ม2 ค้าง = ตกลง
#pragma once
#include "gfx.h"
#include "pages.h"
#include "DidissnapConfig.h"

namespace menu {
using namespace gfx;

struct Item { const char* label; const char* key; int mn, mx, st; const char* desc; };
// ---- หมวดต่าง ๆ (หน้าแรกของเมนูเป็นช่องไอคอน 2x3) ----
static const Item L_DISP[] = {
    {"BRIGHTNESS", "bri", 10, 100, 10, "Screen brightness"},
    {"AUTO BRIGHT", "briAuto", 0, 1, 1, "Brightness follows the light sensor"},
    {"FLIP 180", "flip", 0, 1, 1, "Turn the picture upside down"},
    {"REFRESH", "lcdMode", 0, 3, 1, "Screen refresh rate (board restarts)"},
    {"PERF INFO", "perf", 0, 1, 1, "Show fps / timing text at the bottom"},
    {"BACK", nullptr, 0, 0, 0, "Back to the menu"},
};
static const Item L_PAGES[] = {
    {"PAGE", "page", 0, pages::N - 1, 1, "Page shown now (also after restart)"},
    {"AUTO CYCLE", "autoCycle", 0, 120, 5, "Change page by itself every N seconds"},
    {"P13 SLOT 1", "p13a", 0, 0, 1, "Page 13 bottom row - value 1"},
    {"P13 SLOT 2", "p13b", 0, 0, 1, "Page 13 bottom row - value 2"},
    {"P13 SLOT 3", "p13c", 0, 0, 1, "Page 13 bottom row - value 3"},
    {"P13 SLOT 4", "p13d", 0, 0, 1, "Page 13 bottom row - value 4"},
    {"BACK", nullptr, 0, 0, 0, "Back to the menu"},
};
static const Item L_OBD[] = {
    {"OBD BLE", "obdBle", 0, 1, 1, "Bluetooth LE OBD adapter on/off (board restarts)"},
    {"READ DTC", nullptr, 0, 0, 0, "Read stored fault codes from the car"},
    {"CLEAR DTC", nullptr, 0, 0, 0, "Erase fault codes (engine OFF, key ON)"},
    {"FORGET ADAPTER", nullptr, 0, 0, 0, "Forget the saved adapter and search again"},
    {"BACK", nullptr, 0, 0, 0, "Back to the menu"},
};
static const Item L_CAR[] = {
    {"REDLINE", "maxRpm", 2000, 12000, 250, "Shift light / red zone starts here"},
    {"TEMP ALERT", "tempAlert", 70, 130, 1, "Coolant warning temperature"},
    {"GEARS", "gearCount", 3, 6, 1, "Number of gears for the gear display"},
    {"BACK", nullptr, 0, 0, 0, "Back to the menu"},
};
static const Item L_RESET[] = {
    {"PEAK RESET", nullptr, 0, 0, 0, "Clear max RPM / speed / temperatures"},
    {"TRIP RESET", nullptr, 0, 0, 0, "Set the trip distance back to 0"},
    {"BACK", nullptr, 0, 0, 0, "Back to the menu"},
};
static const Item L_ADV[] = {
    {"DRAW CORES", "cores", 1, 2, 1, "Draw the screen with 1 or 2 CPU cores"},
    {"COPY SPEED", "copySpd", 10, 80, 10, "PSRAM copy speed limit (screen stability)"},
    {"LCD BOUNCE", "bounce", 0, 1, 1, "LCD bounce buffers (board restarts)"},
    {"BAND SKIP", "bandSkip", 0, 1, 1, "Skip unchanged screen bands (test)"},
    {"GIF TEST", "gifTest", 0, 4, 1, "GIF decoder test modes"},
    {"BACK", nullptr, 0, 0, 0, "Back to the menu"},
};
enum { IC_SUN, IC_PAGES, IC_BT, IC_CAR, IC_RESET, IC_GEAR };
struct Cat { const char* name; const Item* it; int n; int icon; };
#define MCAT(nm, arr, ic) {nm, arr, (int)(sizeof(arr) / sizeof(arr[0])), ic}
static const Cat CATS[] = {MCAT("DISPLAY", L_DISP, IC_SUN), MCAT("PAGES", L_PAGES, IC_PAGES), MCAT("OBD", L_OBD, IC_BT),
                           MCAT("CAR", L_CAR, IC_CAR), MCAT("RESET", L_RESET, IC_RESET), MCAT("ADVANCED", L_ADV, IC_GEAR)};
static const int NC = sizeof(CATS) / sizeof(CATS[0]);
static const int N = NC;
static int cat = -1;          // -1 = หน้าช่องไอคอน, 0.. = อยู่ในหมวด
static int gsel = 0;          // ช่องที่เลือกในหน้าช่องไอคอน
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
// DTC บนจอ (ไม่บล็อกการวาด): เริ่ม 1 = อ่าน / 2 = ลบ · สถานะ 0 ว่าง, 1 กำลังทำ, 2 สำเร็จ, 3 ไม่สำเร็จ (buf = ข้อความผล)
static bool (*dtcStart)(int) = nullptr;
static int (*dtcPoll)(char*, size_t) = nullptr;
static bool obdLive = false;                    // ได้ค่าจากรถอยู่ (ตั้งจาก main ทุกเฟรม)
static uint32_t clearArm = 0;                   // กด CLEAR DTC ครั้งแรก = เตรียม, ครั้งที่สองใน 3 วิ = ลบจริง
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
  if (!it.key) { snprintf(b, n, !strcmp(it.label, "BACK") ? "<" : ">"); return; }
  int v = value(it.key);
  if (!strcmp(it.key, "page")) snprintf(b, n, "%d %s", v + 1, pages::NAMES[v]);
  else if (!strcmp(it.key, "bri")) snprintf(b, n, "%d%%", v);
  else if (!strcmp(it.key, "maxRpm")) snprintf(b, n, "%d rpm", v);
  else if (!strcmp(it.key, "tempAlert")) snprintf(b, n, "%d C", v);
  else if (!strcmp(it.key, "autoCycle")) { if (v) snprintf(b, n, "%d s", v); else snprintf(b, n, "OFF"); }
  else if (!strncmp(it.key, "p13", 3)) snprintf(b, n, "%s", didicfg::SLOT_NAMES[v % didicfg::N_SLOTS]);
  else if (!strcmp(it.key, "obdBle")) snprintf(b, n, "%s", v ? "ON" : "OFF");
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

// ---- ไอคอนเส้นหนา (วาดด้วยกราฟิก ไม่ใช้รูป) ----
inline void drawIcon(int k, float cx, float cy, uint16_t c, uint16_t bg) {
  switch (k) {
    case IC_SUN:
      circle(cx, cy, 10, bg, c, 3);
      for (int a = 0; a < 8; a++) { float t = a * (float)M_PI / 4; line(cx + 15 * cosf(t), cy + 15 * sinf(t), cx + 20 * cosf(t), cy + 20 * sinf(t), c, 3); }
      break;
    case IC_PAGES:
      rrect(cx - 8, cy - 22, 26, 22, 3, bg, c, 3);
      rrect(cx - 16, cy - 14, 26, 22, 3, bg, c, 3);
      break;
    case IC_BT: {
      const float x[] = {-8, 8, 0, 0, 8, -8}, y[] = {-8, 8, 16, -16, -8, 8};
      for (int i = 0; i < 5; i++) line(cx + x[i], cy + y[i], cx + x[i + 1], cy + y[i + 1], c, 3);
      break;
    }
    case IC_CAR:
      rrect(cx - 20, cy - 4, 40, 12, 4, bg, c, 3);
      line(cx - 12, cy - 4, cx - 6, cy - 13, c, 3); line(cx - 6, cy - 13, cx + 8, cy - 13, c, 3); line(cx + 8, cy - 13, cx + 14, cy - 4, c, 3);
      circle(cx - 11, cy + 10, 5, c); circle(cx + 11, cy + 10, 5, c);
      break;
    case IC_RESET: {
      arc(cx, cy, 15, 0.7f, 5.8f, c, 3);
      const float px[] = {cx + 16, cx + 16, cx + 6}, py[] = {cy - 15, cy - 2, cy - 6};
      poly(px, py, 3, c);
      break;
    }
    case IC_GEAR:
      for (int a = 0; a < 8; a++) { float t = a * (float)M_PI / 4; line(cx + 10 * cosf(t), cy + 10 * sinf(t), cx + 18 * cosf(t), cy + 18 * sinf(t), c, 6); }
      circle(cx, cy, 12, bg, c, 4);
      break;
  }
}
// สรุปค่าของหมวด (บรรทัดเล็กใต้ชื่อในช่อง)
inline void catSummary(int k, char* b, size_t n) {
  const auto& c = didicfg::cfg;
  switch (k) {
    case 0: snprintf(b, n, "%d%%  %s", c.bri, lcdMode == 1 ? "30 Hz" : (lcdMode >= 2 ? "60 Hz" : "44 Hz")); break;
    case 1: snprintf(b, n, "page %d", c.page + 1); break;
    case 2: snprintf(b, n, "%s", obdMode ? (obdStatus ? obdStatus() : "on") : "off"); break;
    case 3: snprintf(b, n, "redline %d", c.maxRpm); break;
    case 4: snprintf(b, n, "peak / trip"); break;
    default: snprintf(b, n, "tuning"); break;
  }
}
inline bool isSlider(const char* k) {
  return k && (!strcmp(k, "bri") || !strcmp(k, "maxRpm") || !strcmp(k, "tempAlert") || !strcmp(k, "autoCycle") || !strcmp(k, "copySpd"));
}
inline void header(const char* t) {
  fill(0, 0, W, 28, rgb(0x1A1512));
  text(t, 12, 20, 16, pages::ORG, LEFT, 900);
  text("WiFi Didissnap  192.168.4.1", 468, 19, 11, rgb(0x999999), RIGHT, 600);
}
inline void footer(const char* t) {
  fill(0, H - 22, W, 22, rgb(0x1A1512));
  text(t, W / 2, H - 7, 11, rgb(0xBBBBBB), CENTER, 600);
}

inline void drawGrid() {
  using pages::ORG;
  header("MENU");
  for (int i = 0; i < NC; i++) {
    int x = 8 + (i % 3) * 156, y = 36 + (i / 3) * 106;
    bool s = i == gsel;
    uint16_t bg = s ? rgb(0x2A1E12) : rgb(0x18181A);
    rrect(x, y, 150, 98, 10, bg, s ? ORG : rgb(0x37373C), s ? 2.5f : 1);
    drawIcon(CATS[i].icon, x + 75, y + 34, s ? ORG : rgb(0xC8C8C8), bg);
    text(CATS[i].name, x + 75, y + 74, 16, s ? rgb(0xFFB37A) : rgb(0xF0F0F0), CENTER, 800);
    char v[32];
    catSummary(i, v, sizeof(v));
    text(v, x + 75, y + 90, 11, rgb(0x969696), CENTER, 600);
  }
  footer("BTN1 = NEXT   BTN2 = PREV   HOLD BTN2 = OPEN   HOLD BTN1 = EXIT");
}

// กล่องสถานะด้านบนของหมวด OBD (+ ผล DTC)
inline void obdBox() {
  static char dtc[64] = "";
  int st = dtcPoll ? dtcPoll(dtc, sizeof(dtc)) : 0;
  const char* d = obdStatus ? obdStatus() : "";
  uint16_t c, fc;
  const char* big;
  if (!obdMode) { c = rgb(0x555555); fc = rgb(0x161616); big = "OBD OFF"; }
  else if (obdLive) { c = rgb(0x3CC86E); fc = rgb(0x10201A); big = "CONNECTED"; }
  else { c = rgb(0xFFC83C); fc = rgb(0x221C0C); big = "SEARCHING"; }
  rrect(8, 34, 464, 76, 10, fc, c, 2);
  drawIcon(IC_BT, 42, 72, c, fc);
  if (st == 1) { text("WORKING...", 80, 64, 20, rgb(0xFFC83C), LEFT, 900); text("talking to the car", 80, 92, 13, rgb(0xB4C8B4), LEFT, 600); }
  else if (st >= 2) {
    text(st == 2 ? "DTC" : "DTC FAILED", 80, 64, 20, st == 2 ? rgb(0xFFC83C) : rgb(0xFF5050), LEFT, 900);
    text(dtc, 80, 92, 13, rgb(0xF0F0F0), LEFT, 700);
  } else { text(big, 80, 64, 20, c, LEFT, 900); text(d, 80, 92, 13, rgb(0xB4C8B4), LEFT, 600); }
}

inline void drawList() {
  using pages::ORG;
  const Cat& C = CATS[cat];
  char t[32];
  snprintf(t, sizeof(t), "MENU > %s", C.name);
  header(t);
  bool ob = C.it == L_OBD;
  if (ob) obdBox();
  const int y0 = ob ? 116 : 33, rh = ob ? 33 : 36, rows = ob ? 4 : 6;
  int top = max(0, min(sel - (rows - 1) / 2, C.n - rows));
  bool bl = (nowMs() / 250) % 2 == 0;
  for (int r = 0; r < rows; r++) {
    int j = top + r;
    if (j >= C.n) break;
    const Item& it = C.it[j];
    int y = y0 + r * rh;
    bool s = j == sel;
    if (s) rrect(6, y, 468, rh - 3, 5, edit ? rgb(0x5A2E00) : rgb(0x2A2320), edit ? ORG : rgb(0x8A4A10), 2);
    int cy = y + (rh - 3) / 2 + 6;
    text(it.label, 18, cy, 16, s ? rgb(0xFFB37A) : rgb(0xF0F0F0), LEFT, 800);
    char v[40];
    fmt(it, v, sizeof(v));
    uint16_t vc = s && edit ? (bl ? 0xFFFF : ORG) : rgb(0xAAAAAA);
    if (isSlider(it.key)) {
      float f = (float)(value(it.key) - it.mn) / (it.mx - it.mn);
      int sx = 250, sw = 140, sy = y + (rh - 3) / 2 - 4;
      rrect(sx, sy, sw, 9, 4.5f, rgb(0x2D2D2D));
      rrect(sx, sy, fmaxf(9, sw * fminf(1, fmaxf(0, f))), 9, 4.5f, ORG);
      text(v, 464, cy, 14, vc, RIGHT, 700);
    } else text(v, 464, cy, 14, vc, RIGHT, 600);
  }
  footer(edit ? "BTN1 = +     BTN2 = -     HOLD BTN2 = OK" : C.it[sel].desc);
}

inline void draw() {
  clear(rgb(0x0A0A0A));
  if (cat < 0) drawGrid();
  else drawList();
}

inline void drawToast() {
  if (nowMs() >= toastUntil) return;
  rrect(W / 2 - 90, H / 2 - 18, 180, 36, 8, rgb(0x101010), pages::ORG, 1.5f);
  text(toastMsg, W / 2, H / 2 + 6, 16, pages::ORG, CENTER, 800);
}

// ปุ่ม: b = 0 (ปุ่ม 1) / 1 (ปุ่ม 2)
inline void button(int b, bool lng) {
  if (!open) {
    if (b == 0 && lng) { open = true; cat = -1; gsel = 0; sel = 0; edit = false; return; }
    if (lng) { if (onHold2) onHold2(); return; }
    int p = didicfg::cfg.page + (b == 0 ? 1 : -1);
    didicfg::setPage(((p % pages::N) + pages::N) % pages::N);
    return;
  }
  if (cat < 0) {                              // หน้าช่องไอคอน
    if (b == 0 && lng) { open = false; return; }
    if (lng) { cat = gsel; sel = 0; edit = false; return; }
    gsel = (gsel + (b == 0 ? 1 : NC - 1)) % NC;
    return;
  }
  const Cat& C = CATS[cat];
  const Item& it = C.it[sel];
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
  if (b == 0 && lng) { cat = -1; return; }   // ปุ่ม 1 ค้าง = กลับหน้าช่องไอคอน
  if (!lng) { sel = (sel + (b == 0 ? 1 : C.n - 1)) % C.n; return; }
  // ปุ่ม 2 ค้าง = เลือก
  if (it.key) edit = true;
  else if (!strcmp(it.label, "BACK")) cat = -1;
  else if (!strcmp(it.label, "PEAK RESET")) { if (onPeaks) onPeaks(); toast("PEAK RESET"); }
  else if (!strcmp(it.label, "TRIP RESET")) { if (onTrip) onTrip(); toast("TRIP RESET"); }
  else if (!strcmp(it.label, "FORGET ADAPTER")) { if (onObdForget) onObdForget(); }
  else if (!strcmp(it.label, "READ DTC")) { if (!dtcStart || !dtcStart(1)) toast("OBD NOT CONNECTED"); }
  else if (!strcmp(it.label, "CLEAR DTC")) {
    if (!clearArm || millis() - clearArm > 3000) { clearArm = millis(); toast("HOLD AGAIN TO CLEAR"); }
    else { clearArm = 0; if (!dtcStart || !dtcStart(2)) toast("OBD NOT CONNECTED"); }
  }
}

}  // namespace menu
